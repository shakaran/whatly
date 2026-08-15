// Tray icon, actions, and window-title/notification-count handling.
#include "mainwindow.h"
#include "utils.h"
#include "appprofile.h"
#include "common.h"
#include "detachedaccountwindow.h" // windowLabel() compares against a real window
#include "trayicon.h"

#include <algorithm>

#include <QStyleHints>
#include <QDateTime>
#include <QActionGroup>
#include <QPainter>
#include <QIcon>

#include "shortcuts.h"
#include "chatnav.h"
#include "dictionaries.h"
#include "webengineprofilemanager.h"
#include "aiassistant.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

// ── Actions ──────────────────────────────────────────────────────────────────

void MainWindow::createActions() {
  m_openUrlAction = new QAction(tr("New Chat"), this);
  m_openUrlAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_N));
  connect(m_openUrlAction, &QAction::triggered, this, &MainWindow::newChat);
  addAction(m_openUrlAction);

  m_fullscreenAction = new QAction(tr("Fullscreen"), this);
  m_fullscreenAction->setShortcut(Qt::Key_F11);
  connect(m_fullscreenAction, &QAction::triggered, m_fullscreenAction,
          [=]() { setWindowState(windowState() ^ Qt::WindowFullScreen); });
  addAction(m_fullscreenAction);

  m_minimizeAction = new QAction(tr("Mi&nimize to tray"), this);
  // Carried by the action itself rather than a detached QShortcut, so the
  // shortcut sheet can read it back instead of hardcoding the key.
  m_minimizeAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_W));
  m_minimizeAction->setAutoRepeat(false);
  // Same guard as closeEvent: hiding is only safe while the tray icon is there
  // to bring the window back. With it hidden there is nothing left to click, and
  // Ctrl+W — which reads as "close tab" in a tabbed app — would strand the
  // window, so fall back to an ordinary minimise to the taskbar. This also
  // covers the "minimize in tray on start" setting, which triggers this action.
  //
  // Every window by default, not just this one — this action IS the tray's own
  // "put Whatly away", and leaving the other windows up while the tray shows the
  // app as away is the same "one window is the real one" that the rest of this
  // branch removes. Which of the two is wanted is taste rather than correctness,
  // though, so "minimizeOnlyFocusedWindow" takes it back to one window.
  connect(m_minimizeAction, &QAction::triggered, this, [this]() {
    const bool onlyFront = SettingsManager::instance()
                               .settings()
                               .value("minimizeOnlyFocusedWindow", false)
                               .toBool();
    if (QSystemTrayIcon::isSystemTrayAvailable() && m_systemTrayIcon &&
        m_systemTrayIcon->isVisible()) {
      if (onlyFront)
        frontWindow()->hide();
      else
        hideAllWindows();
      return;
    }
    // No tray to bring anything back from, so minimise to the taskbar instead —
    // and the window in front rather than this one, which is the same assumption
    // again: with a detached window being worked in, Ctrl+W used to minimise the
    // main window sitting behind it and appear to do nothing.
    QWidget *w = frontWindow();
    w->setWindowState(w->windowState() | Qt::WindowMinimized);
  });
  addAction(m_minimizeAction);

  m_restoreAction = new QAction(tr("&Restore"), this);
  // Restores the whole app, for the same reason: it is the counterpart of the
  // action above, and it used to show this window alone.
  connect(m_restoreAction, &QAction::triggered, this,
          &MainWindow::restoreAllWindows);
  addAction(m_restoreAction);

  m_reloadAction = new QAction(tr("Re&load"), this);
  m_reloadAction->setShortcut(Qt::Key_F5);
  connect(m_reloadAction, &QAction::triggered, this,
          [=]() { this->doReload(); });
  addAction(m_reloadAction);

  m_lockAction = new QAction(tr("Loc&k"), this);
  m_lockAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_L));
  connect(m_lockAction, &QAction::triggered, this, &MainWindow::lockApp);
  addAction(m_lockAction);

  m_muteAction = new QAction(tr("&Mute audio"), this);
  m_muteAction->setCheckable(true);
  m_muteAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_M));
  m_muteAction->setChecked(
      SettingsManager::instance().settings().value("muteAudio", false).toBool());
  connect(m_muteAction, &QAction::toggled, this,
          [this](bool checked) { toggleMute(checked); });
  addAction(m_muteAction);

  m_zoomInAction = new QAction(tr("Zoom in"), this);
  m_zoomInAction->setShortcuts(
      {QKeySequence::ZoomIn, QKeySequence(Qt::Modifier::CTRL | Qt::Key_Equal)});
  connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
  addAction(m_zoomInAction);

  m_zoomOutAction = new QAction(tr("Zoom out"), this);
  m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
  connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
  addAction(m_zoomOutAction);

  m_zoomResetAction = new QAction(tr("Reset zoom"), this);
  m_zoomResetAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_0));
  connect(m_zoomResetAction, &QAction::triggered, this, &MainWindow::zoomReset);
  addAction(m_zoomResetAction);

  // No default shortcut. Ctrl+B was the obvious pick and the wrong one: it is
  // Bold in WhatsApp's message box, so it only worked with the cursor outside
  // the very field you spend the day in. The action is still in the command
  // palette and can be given a key of the user's choosing in Settings.
  m_chatListStripAction = new QAction(this);
  refreshChatListStripAction();
  connect(m_chatListStripAction, &QAction::triggered, this,
          &MainWindow::toggleChatListStrip);
  addAction(m_chatListStripAction);

  // Ctrl+F. WhatsApp Web binds nothing to it: in a browser that key opens the
  // browser's own find bar, and Qt WebEngine has no find bar, so pressing it in
  // Whatly did nothing whatsoever. An app whose whole content is a chat list has
  // to answer Ctrl+F, so we answer it ourselves, with the search box the list
  // already has.
  m_findChatAction = new QAction(tr("&Find in chats"), this);
  m_findChatAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_F));
  // Reaches whichever window has the keyboard, like the palette: the search box
  // wanted is the one in the window being typed into.
  m_findChatAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(m_findChatAction, &QAction::triggered, this,
          &MainWindow::focusChatSearch);
  addAction(m_findChatAction);

  // Which language spelling is checked against, switched from the keyboard while
  // writing (#41). Ctrl+Alt+S because WhatsApp Web binds nothing with Alt in the
  // message box, which is exactly where this key is pressed — and an application
  // shortcut so it answers from a detached account window too.
  m_spellNextAction = new QAction(tr("Spelling: next language"), this);
  m_spellNextAction->setShortcut(
      QKeySequence(Qt::Modifier::CTRL | Qt::Modifier::ALT | Qt::Key_S));
  m_spellNextAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(m_spellNextAction, &QAction::triggered, this,
          &MainWindow::cycleSpellCheckLanguage);
  addAction(m_spellNextAction);

  m_settingsAction = new QAction(tr("&Settings"), this);
  m_settingsAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_P));
  connect(m_settingsAction, &QAction::triggered, this,
          &MainWindow::showSettings);
  addAction(m_settingsAction);

  m_scheduledMessagesAction = new QAction(tr("Scheduled &messages…"), this);
  connect(m_scheduledMessagesAction, &QAction::triggered, this,
          &MainWindow::showScheduledMessages);
  addAction(m_scheduledMessagesAction);

  // Inline translation (idea #6). No default shortcuts — both live in the
  // command palette and can be bound to keys of the user's choosing in Settings.
  m_translateSelectionAction = new QAction(tr("Translate selection"), this);
  m_translateSelectionAction->setIcon(
      QIcon::fromTheme(QStringLiteral("preferences-desktop-locale")));
  connect(m_translateSelectionAction, &QAction::triggered, this,
          &MainWindow::translateSelection);
  addAction(m_translateSelectionAction);

  m_translateComposerAction = new QAction(tr("Translate message box"), this);
  m_translateComposerAction->setIcon(
      QIcon::fromTheme(QStringLiteral("preferences-desktop-locale")));
  connect(m_translateComposerAction, &QAction::triggered, this,
          &MainWindow::translateComposer);
  addAction(m_translateComposerAction);

  // Export the open conversation (idea #9). No default shortcut; command palette
  // and Shortcuts expose it.
  m_exportChatAction = new QAction(tr("Export chat…"), this);
  m_exportChatAction->setIcon(
      QIcon::fromTheme(QStringLiteral("document-save")));
  connect(m_exportChatAction, &QAction::triggered, this,
          &MainWindow::exportCurrentChat);
  addAction(m_exportChatAction);

  // AI assistant (idea #5). No default shortcuts; command palette / Shortcuts /
  // right-click menu. Icons make them explicit in the context menu.
  m_aiSummarizeAction = new QAction(tr("AI: Summarise chat"), this);
  m_aiSummarizeAction->setIcon(
      QIcon::fromTheme(QStringLiteral("view-list-text")));
  connect(m_aiSummarizeAction, &QAction::triggered, this,
          &MainWindow::aiSummarizeChat);
  addAction(m_aiSummarizeAction);

  m_aiImproveAction = new QAction(tr("AI: Improve message"), this);
  m_aiImproveAction->setIcon(
      QIcon::fromTheme(QStringLiteral("tools-check-spelling")));
  connect(m_aiImproveAction, &QAction::triggered, this,
          &MainWindow::aiImproveComposer);
  addAction(m_aiImproveAction);

  m_aiSuggestAction = new QAction(tr("AI: Suggest a reply"), this);
  m_aiSuggestAction->setIcon(
      QIcon::fromTheme(QStringLiteral("mail-reply-sender")));
  connect(m_aiSuggestAction, &QAction::triggered, this,
          &MainWindow::aiSuggestReply);
  addAction(m_aiSuggestAction);

  // One-click tone rewrites of the composer draft (idea #5).
  m_aiFormalAction = new QAction(tr("AI: Make it more formal"), this);
  m_aiFormalAction->setIcon(QIcon::fromTheme(QStringLiteral("format-text-bold")));
  connect(m_aiFormalAction, &QAction::triggered, this,
          [this]() { aiRewrite(Ai::rewriteFormalSystemPrompt()); });
  addAction(m_aiFormalAction);

  m_aiFriendlyAction = new QAction(tr("AI: Make it friendlier"), this);
  m_aiFriendlyAction->setIcon(QIcon::fromTheme(QStringLiteral("face-smile")));
  connect(m_aiFriendlyAction, &QAction::triggered, this,
          [this]() { aiRewrite(Ai::rewriteFriendlySystemPrompt()); });
  addAction(m_aiFriendlyAction);

  m_aiShorterAction = new QAction(tr("AI: Make it shorter"), this);
  m_aiShorterAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-cut")));
  connect(m_aiShorterAction, &QAction::triggered, this,
          [this]() { aiRewrite(Ai::rewriteShorterSystemPrompt()); });
  addAction(m_aiShorterAction);

  m_aiUnreadDigestAction = new QAction(tr("AI: Summarise unread chats"), this);
  m_aiUnreadDigestAction->setIcon(
      QIcon::fromTheme(QStringLiteral("mail-mark-unread")));
  connect(m_aiUnreadDigestAction, &QAction::triggered, this,
          &MainWindow::aiSummarizeUnread);
  addAction(m_aiUnreadDigestAction);

  // Manual Do Not Disturb (idea #10): a checkable toggle plus quick durations,
  // on top of the scheduled window. VIP/keyword still break through.
  m_dndAction = new QAction(tr("Do Not Disturb"), this);
  m_dndAction->setCheckable(true);
  m_dndAction->setIcon(QIcon::fromTheme(QStringLiteral("notifications-disabled")));
  connect(m_dndAction, &QAction::toggled, this, &MainWindow::setDndManual);
  addAction(m_dndAction);

  m_dnd1hAction = new QAction(tr("Do Not Disturb: 1 hour"), this);
  connect(m_dnd1hAction, &QAction::triggered, this,
          [this]() { dndSnoozeFor(60); });
  addAction(m_dnd1hAction);

  m_dnd2hAction = new QAction(tr("Do Not Disturb: 2 hours"), this);
  connect(m_dnd2hAction, &QAction::triggered, this,
          [this]() { dndSnoozeFor(120); });
  addAction(m_dnd2hAction);

  m_dndMorningAction = new QAction(tr("Do Not Disturb: until morning"), this);
  connect(m_dndMorningAction, &QAction::triggered, this,
          &MainWindow::dndSnoozeUntilMorning);
  addAction(m_dndMorningAction);

  // Reflect any manual DND still in force from a previous run.
  refreshDndUi();

  // Reply reminders: snooze the open chat, resurfacing it at a chosen time.
  m_remind1hAction = new QAction(tr("Remind me to reply: in 1 hour"), this);
  m_remind1hAction->setIcon(QIcon::fromTheme(QStringLiteral("appointment-soon")));
  connect(m_remind1hAction, &QAction::triggered, this, [this]() {
    scheduleChatReminder(QDateTime::currentDateTime().addSecs(60 * 60));
  });
  addAction(m_remind1hAction);

  m_remind3hAction = new QAction(tr("Remind me to reply: in 3 hours"), this);
  connect(m_remind3hAction, &QAction::triggered, this, [this]() {
    scheduleChatReminder(QDateTime::currentDateTime().addSecs(3 * 60 * 60));
  });
  addAction(m_remind3hAction);

  m_remindTomorrowAction =
      new QAction(tr("Remind me to reply: tomorrow morning"), this);
  connect(m_remindTomorrowAction, &QAction::triggered, this, [this]() {
    scheduleChatReminder(
        QDateTime(QDate::currentDate().addDays(1), QTime(9, 0)));
  });
  addAction(m_remindTomorrowAction);

  m_toggleThemeAction = new QAction(tr("&Toggle theme"), this);
  m_toggleThemeAction->setShortcut(
      QKeySequence(Qt::Modifier::CTRL | Qt::Key_T));
  connect(m_toggleThemeAction, &QAction::triggered, this,
          &MainWindow::toggleTheme);
  addAction(m_toggleThemeAction);

  // Account layout: Tabs (one at a time) vs Grid (all at once). Separate windows
  // remain available via --profile. An exclusive, checkable pair; Ctrl+G flips.
  auto *viewGroup = new QActionGroup(this);
  viewGroup->setExclusive(true);
  m_viewTabsAction = new QAction(tr("Tabbed view"), this);
  m_viewTabsAction->setCheckable(true);
  m_viewTabsAction->setActionGroup(viewGroup);
  connect(m_viewTabsAction, &QAction::triggered, this,
          [this]() { setViewMode(ViewMode::Tabs); });
  addAction(m_viewTabsAction);

  m_viewGridAction = new QAction(tr("Grid view"), this);
  m_viewGridAction->setCheckable(true);
  m_viewGridAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_G));
  m_viewGridAction->setActionGroup(viewGroup);
  connect(m_viewGridAction, &QAction::triggered, this, [this]() {
    // Ctrl+G toggles: if already in grid, go back to tabs.
    setViewMode(viewMode() == ViewMode::Grid ? ViewMode::Tabs
                                             : ViewMode::Grid);
  });
  addAction(m_viewGridAction);

  // A visible way to add another account (the "+" tab only shows once the
  // account strip is up); mirrors the command palette's "Add account…".
  m_addAccountAction = new QAction(tr("Add account…"), this);
  // Through a lambda rather than a direct member pointer: promptAddAccount takes
  // an optional target window, and triggered(bool) cannot bind past a default
  // argument. No target here means "the focused window", which is what this
  // action wants anyway.
  connect(m_addAccountAction, &QAction::triggered, this,
          [this]() { promptAddAccount(); });
  addAction(m_addAccountAction);

  m_commandPaletteAction = new QAction(tr("Command palette"), this);
  m_commandPaletteAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_K));
  // Application-wide so Ctrl+K opens the palette from a detached window too, not
  // only from the main window.
  m_commandPaletteAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(m_commandPaletteAction, &QAction::triggered, this,
          &MainWindow::showCommandPalette);
  addAction(m_commandPaletteAction);

  m_aboutAction = new QAction(tr("&About"), this);
  // The only way to this dialog used to be the tray menu, and the tray is
  // exactly what is missing or misbehaving on the desktops people file bugs
  // from — so the version number, commit and build date they were being asked
  // for were unreachable precisely when they needed them. F1 always works.
  m_aboutAction->setShortcut(QKeySequence::HelpContents);
  connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
  addAction(m_aboutAction);

  m_quitAction = new QAction(tr("&Quit"), this);
  m_quitAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_Q));
  connect(m_quitAction, &QAction::triggered, this, &MainWindow::quitApp);
  addAction(m_quitAction);

  // Register each action's current (hard-coded) shortcut as its default, then
  // apply any stored user override. The Settings dialog edits these; changes
  // take effect on the next launch.
  const struct {
    QAction *action;
    const char *id;
    QString label;
  } shortcutRegistry[] = {
      {m_reloadAction, "reload", tr("Reload")},
      {m_minimizeAction, "minimize", tr("Minimise to tray")},
      {m_lockAction, "lock", tr("Lock")},
      {m_muteAction, "mute", tr("Mute audio")},
      {m_fullscreenAction, "fullscreen", tr("Fullscreen")},
      {m_openUrlAction, "openChat", tr("New chat / open URL")},
      {m_zoomResetAction, "zoomReset", tr("Reset zoom")},
      // Registered with no default so it appears in the shortcut sheet and can
      // be bound to whatever the user likes.
      {m_chatListStripAction, "chatListStrip", tr("Collapse the chat list")},
      {m_findChatAction, "findChat", tr("Find in chats")},
      {m_spellNextAction, "spellNext", tr("Spelling: next language")},
      {m_translateSelectionAction, "translateSelection",
       tr("Translate selection")},
      {m_translateComposerAction, "translateComposer",
       tr("Translate message box")},
      {m_exportChatAction, "exportChat", tr("Export chat")},
      {m_aiSummarizeAction, "aiSummarize", tr("AI: Summarise chat")},
      {m_aiImproveAction, "aiImprove", tr("AI: Improve message")},
      {m_aiSuggestAction, "aiSuggest", tr("AI: Suggest a reply")},
      {m_aiUnreadDigestAction, "aiUnreadDigest",
       tr("AI: Summarise unread chats")},
      {m_aiFormalAction, "aiFormal", tr("AI: Make it more formal")},
      {m_aiFriendlyAction, "aiFriendly", tr("AI: Make it friendlier")},
      {m_aiShorterAction, "aiShorter", tr("AI: Make it shorter")},
      {m_dndAction, "dnd", tr("Do Not Disturb")},
      {m_dnd1hAction, "dnd1h", tr("Do Not Disturb: 1 hour")},
      {m_dnd2hAction, "dnd2h", tr("Do Not Disturb: 2 hours")},
      {m_dndMorningAction, "dndMorning", tr("Do Not Disturb: until morning")},
      {m_remind1hAction, "remind1h", tr("Remind me to reply: in 1 hour")},
      {m_remind3hAction, "remind3h", tr("Remind me to reply: in 3 hours")},
      {m_remindTomorrowAction, "remindTomorrow",
       tr("Remind me to reply: tomorrow morning")},
      {m_settingsAction, "settings", tr("Settings")},
      {m_toggleThemeAction, "toggleTheme", tr("Toggle theme")},
      {m_viewGridAction, "gridView", tr("Grid view")},
      {m_commandPaletteAction, "commandPalette", tr("Command palette")},
      {m_quitAction, "quit", tr("Quit")},
  };
  for (const auto &r : shortcutRegistry) {
    if (!r.action)
      continue;
    Shortcuts::registerAction(QString::fromLatin1(r.id), r.label,
                              r.action->shortcut());
    r.action->setShortcut(Shortcuts::get(QString::fromLatin1(r.id)));
  }
}

// ── Tray icon ─────────────────────────────────────────────────────────────────

void MainWindow::createTrayIcon() {
  m_trayIconMenu = new QMenu(this);
  m_trayIconMenu->setObjectName("trayIconMenu");
  m_trayIconMenu->addAction(m_minimizeAction);
  m_trayIconMenu->addAction(m_restoreAction);
  // Every window, numbered by how recently it was used, so each one can be
  // reached directly. With no "main" window there is no window the tray is
  // guaranteed to bring up, and a window can end up behind everything else or
  // minimised with nothing pointing at it; a list of them is the handle. It hides
  // itself while there is only one window, which is a list of nothing useful.
  m_windowsMenu = m_trayIconMenu->addMenu(tr("Windows"));
  m_windowsMenu->menuAction()->setVisible(false);
  m_trayIconMenu->addSeparator();
  // Recent unread chats (idea #3): populated live from the active account; the
  // submenu hides itself when there is nothing unread.
  m_recentUnreadMenu = m_trayIconMenu->addMenu(tr("Recent unread"));
  m_recentUnreadMenu->menuAction()->setVisible(false);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_reloadAction);
  m_trayIconMenu->addAction(m_lockAction);
  m_trayIconMenu->addAction(m_muteAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_openUrlAction);
  m_trayIconMenu->addAction(m_scheduledMessagesAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_viewTabsAction);
  m_trayIconMenu->addAction(m_viewGridAction);
  m_trayIconMenu->addAction(m_addAccountAction);
  m_trayIconMenu->addSeparator();
  // Which language spelling is checked against. Built each time it opens, and
  // hidden while there is only one language to check against — a list of one is
  // not a switch. The keyboard reaches the same thing (m_spellNextAction).
  m_spellingMenu = m_trayIconMenu->addMenu(tr("Spelling"));
  m_trayIconMenu->addAction(m_toggleThemeAction);
  m_trayIconMenu->addAction(m_settingsAction);
  m_trayIconMenu->addAction(m_aboutAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_quitAction);

  // Anything picked from the tray menu is a request to use Whatly, so bring it
  // to the front — it used to run with the window still buried, which is
  // baffling for "Settings" or "New chat". The three that are ABOUT the window
  // not being up are the exceptions: quitting, minimising, and the theme
  // toggle, which is worth having without stealing focus.
  connect(m_trayIconMenu, &QMenu::triggered, this, [this](QAction *action) {
    if (action == m_quitAction || action == m_minimizeAction ||
        action == m_toggleThemeAction)
      return;
    // The window list names the window to come up, and this signal reaches
    // submenu entries too — so without this the front window would be hauled up
    // first and the chosen one only afterwards.
    if (m_windowsMenu && m_windowsMenu->actions().contains(action))
      return;
    raiseWindow();
    // Settings opens a window of its own, and raising the main window above it
    // is worse than not raising anything: the page you asked for ends up behind
    // and without the keyboard. Put it back on top once this menu has unwound.
    if (action == m_settingsAction)
      QTimer::singleShot(0, this, [this]() {
        if (m_settingsWidget && m_settingsWidget->isVisible()) {
          m_settingsWidget->raise();
          m_settingsWidget->activateWindow();
        }
      });
  });

  m_systemTrayIcon = new QSystemTrayIcon(m_trayIconNormal, this);
  m_systemTrayIcon->setContextMenu(m_trayIconMenu);
  connect(m_trayIconMenu, &QMenu::aboutToShow, this,
          &MainWindow::checkWindowState);
  // On the whole menu opening, not on the submenu: whether "Spelling" is there at
  // all has to be settled before the pointer reaches it.
  connect(m_trayIconMenu, &QMenu::aboutToShow, this,
          &MainWindow::rebuildSpellingMenu);
  connect(m_systemTrayIcon, &QSystemTrayIcon::activated, this,
          &MainWindow::iconActivated);

  // Do NOT connect QSystemTrayIcon::messageClicked here under Linux, however
  // tempting it looks. Qt's QDBusTrayIcon subscribes to the *global*
  // org.freedesktop.Notifications signals and emits messageClicked() from
  // actionInvoked() without checking that the notification id is one of its
  // own — so clicking a notification from any other application on the desktop
  // fires it. That is what used to raise this window when someone clicked a
  // notification from their mail client. On Linux, notification clicks come
  // from libnotify-qt instead, which does match the id (see the notification
  // presenter); the messageClicked path is only wired up on other platforms,
  // where the signal belongs to our own toast.

  // Hidden on request — but only when the window can still be reached another
  // way (closeEvent forces "quit" while the tray is hidden, see below).
  if (!SettingsManager::instance()
           .settings()
           .value("hideTrayIcon", false)
           .toBool())
    m_systemTrayIcon->show();

  if (qApp->styleHints()->showShortcutsInContextMenus()) {
    foreach (QAction *action, m_trayIconMenu->actions()) {
      action->setShortcutVisibleInContextMenu(true);
    }
  }
}

// Act on the actions themselves rather than on menu->actions().at(0/1/4): those
// indices count the separators too, so they happen to be right only as long as
// nobody reorders the menu — after which this would silently disable the wrong
// entries.
//
// "Restore" is deliberately never disabled. It used to be greyed out whenever
// the window was visible, and the state was only refreshed from the menu's
// aboutToShow signal. Qt does not guarantee that signal for a tray menu the
// desktop shell renders itself (Wayland exports it over D-Bus), so a stale
// "disabled" could survive the window being hidden — and then there was no way
// left to bring the window back at all. Restoring an already-visible window is
// harmless: it just raises it. Enabling it unconditionally removes the trap
// rather than relying on the refresh always happening.
void MainWindow::checkWindowState() {
  // Any window, not this one: with this window hidden and another in front,
  // "Minimise to tray" was greyed out while there was plainly something to put
  // away, and putting it away is exactly what it now does.
  bool visible = false;
  for (QWidget *w : allWindows())
    if (w->isVisible()) {
      visible = true;
      break;
    }
  if (m_minimizeAction)
    m_minimizeAction->setEnabled(visible);
  if (m_restoreAction)
    m_restoreAction->setEnabled(true);
  if (m_lockAction)
    m_lockAction->setEnabled(!(m_lockWidget && m_lockWidget->getIsLocked()));
  refreshWindowsMenu();
}

// What a window is called in that list: the accounts it holds. A title would be
// the WhatsApp page title, which is the chat being read and changes under the
// user; the accounts are what they arranged the windows BY.
QString MainWindow::windowLabel(const QWidget *w) const {
  QStringList names;
  for (const Account &a : m_accounts) {
    const bool here = a.window
                          ? static_cast<const QWidget *>(a.window.data()) == w
                          : w == this;
    if (here && !a.name.isEmpty())
      names << a.name;
  }
  if (names.isEmpty())
    return QApplication::applicationDisplayName();
  QString label = names.join(QStringLiteral(", "));
  if (label.size() > 40)
    label = label.left(39) + QChar(0x2026);
  return label;
}

// Entries are REUSED, never cleared and rebuilt, and that is a correctness
// requirement rather than an economy: picking one brings a window forward, which
// makes it the most recently used, which comes straight back here — and deleting
// the action whose signal is still being delivered is a crash. So the pool only
// grows, spare entries are hidden, and each entry's target is remembered
// alongside it so a click goes to the window the label described.
void MainWindow::refreshWindowsMenu() {
  if (!m_windowsMenu)
    return;
  const QList<QWidget *> order = windowsByFocus();
  QList<QAction *> entries = m_windowsMenu->actions();
  while (entries.size() < order.size()) {
    const int slot = entries.size();
    QAction *entry = m_windowsMenu->addAction(QString());
    connect(entry, &QAction::triggered, this, [this, slot]() {
      // Weakly held: a detached window can be closed between the menu being
      // filled in and an entry being picked, and closing one hands its accounts
      // back rather than taking them with it — so a stale entry does nothing.
      if (QWidget *target = m_windowsMenuTargets.value(slot))
        bringForward(target);
    });
    entries << entry;
  }
  m_windowsMenuTargets.clear();
  for (int i = 0; i < entries.size(); ++i) {
    if (i >= order.size()) {
      entries[i]->setVisible(false);
      continue;
    }
    QString text = QStringLiteral("%1. %2").arg(i + 1).arg(windowLabel(order[i]));
    // Hidden and minimised windows are the case this list exists for, so say
    // which ones those are rather than leaving the user to guess.
    if (!order[i]->isVisible())
      text += QStringLiteral("  (") + tr("hidden") + QStringLiteral(")");
    else if (order[i]->isMinimized())
      text += QStringLiteral("  (") + tr("minimised") + QStringLiteral(")");
    entries[i]->setText(text);
    entries[i]->setVisible(true);
    m_windowsMenuTargets << QPointer<QWidget>(order[i]);
  }
  m_windowsMenu->menuAction()->setVisible(order.size() > 1);
}

// ── Spell-check language (#41) ─────────────────────────────────────────────────

void MainWindow::rebuildSpellingMenu() {
  if (!m_spellingMenu)
    return;
  const QStringList chosen = Dictionaries::selectedDictionaries();
  // One language is not something to switch between, and none at all means there
  // is no checker running to switch anything on.
  m_spellingMenu->menuAction()->setVisible(chosen.size() > 1);
  if (chosen.size() < 2)
    return;

  m_spellingMenu->clear();
  // clear() takes the entries and leaves the group they were in behind, so a
  // session's worth of menu openings would pile them up.
  qDeleteAll(m_spellingMenu->findChildren<QActionGroup *>());
  auto *group = new QActionGroup(m_spellingMenu); // radio marks, not tick boxes
  const QString focus = Dictionaries::focusedDictionary();
  for (const QString &code : chosen) {
    QAction *entry = m_spellingMenu->addAction(Dictionaries::languageLabel(code));
    entry->setCheckable(true);
    entry->setChecked(code == focus);
    entry->setActionGroup(group);
    connect(entry, &QAction::triggered, this,
            [this, code]() { applySpellCheckFocus(code); });
  }
  m_spellingMenu->addSeparator();
  // All of them at once is Chromium's own behaviour and the state to come back
  // to, not a fourth language: a message written in two of them wants both.
  QAction *all = m_spellingMenu->addAction(tr("All of them"));
  all->setCheckable(true);
  all->setChecked(focus.isEmpty());
  all->setActionGroup(group);
  connect(all, &QAction::triggered, this,
          [this]() { applySpellCheckFocus(QString()); });
}

void MainWindow::cycleSpellCheckLanguage() {
  const QStringList chosen = Dictionaries::selectedDictionaries();
  if (chosen.size() < 2) {
    // Pressed with nothing to switch to. Say why rather than doing nothing at
    // all, since the key gave no sign either way.
    showNotification(QApplication::applicationDisplayName(),
                     chosen.isEmpty()
                         ? tr("No spell-check language is installed.")
                         : tr("Only one spell-check language is chosen. Pick "
                              "more in Settings to switch between them."));
    return;
  }
  applySpellCheckFocus(
      Dictionaries::nextFocus(chosen, Dictionaries::focusedDictionary()));
}

void MainWindow::applySpellCheckFocus(const QString &code) {
  Dictionaries::setFocusedDictionary(code);
  // Live: the profiles keep their pages, and Chromium picks the new language list
  // up without a reload.
  WebEngineProfileManager::instance().applyUserSettings();
  // A Settings page left open is looking at the language box this just changed the
  // meaning of, and it has no way to know: it only re-reads that line when the
  // picker itself is used. Without this it goes on claiming "3 languages" while
  // one of the three is doing the checking.
  if (m_settingsWidget)
    m_settingsWidget->updateSpellCheckSummary();
  // A key pressed in the middle of a sentence has to answer for itself; the tray
  // submenu is not where someone typing is looking.
  showNotification(QApplication::applicationDisplayName(),
                   code.isEmpty()
                       ? tr("Spelling: every chosen language")
                       : tr("Spelling: %1")
                             .arg(Dictionaries::languageLabel(code)));
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
  // Only a left click or double click on the icon acts; the context menu and
  // hover do not.
  if (reason != QSystemTrayIcon::Trigger &&
      reason != QSystemTrayIcon::DoubleClick)
    return;

  // "Frontmost" means shown, not minimised, and focused. A click brings the
  // window to the front reliably (raise + activate + clear minimised) — the
  // part that used to fail on Windows and when minimised. Only when it is
  // already frontmost, and the user opted into it, does a click hide it again.
  // On Windows the tray click hands focus to the shell before this runs, so
  // isActiveWindow() is already false for a window that WAS frontmost; a short
  // grace window (m_lastDeactivationMs, stamped in changeEvent) recovers that,
  // so "hide when clicked while frontmost" still works. The grace is
  // Windows-only — elsewhere isActiveWindow() is reliable here, so keep the
  // behaviour identical to before this change.
#ifdef Q_OS_WIN
  constexpr int kFrontmostGraceMs = 300;
#else
  constexpr int kFrontmostGraceMs = 0;
#endif
  // Act on the window the user last touched, not on this one. This window owns
  // the tray icon, but nothing about the icon says so, and hauling it out from
  // behind the window actually being worked in is the clearest way of telling the
  // user that one of their windows is secretly special.
  QWidget *front = frontWindow();
  const bool frontmost =
      front->isVisible() && !front->isMinimized() &&
      Utils::wasFrontmostRecently(front->isActiveWindow(), m_lastDeactivationMs,
                                  QDateTime::currentMSecsSinceEpoch(),
                                  kFrontmostGraceMs);
  const bool minimizeOnClick = SettingsManager::instance()
                                   .settings()
                                   .value("minimizeOnTrayIconClick", false)
                                   .toBool();
  if (frontmost) {
    if (minimizeOnClick) {
      lockOnHideIfEnabled();
      // Every window, not just this one: hiding one and leaving the others is
      // itself a statement about which window matters.
      hideAllWindows();
    }
    return;
  }
  // All of it, not just the front one. The click above hides every window, so
  // this has to show every window: bringing back only the one last used left the
  // rest hidden, with no window to click and nothing in the tray pointing at
  // them, so they stayed buried until the app was restarted.
  restoreAllWindows();
}

// The tray icon in three independent dimensions: monochrome vs the colourful
// green (a long-standing request — the only bright icon in an otherwise
// monochrome tray), connected vs not (so a silent disconnect after boot or
// resume is visible instead of being noticed hours later), and the unread
// count. Composed here from one source rather than shipping a matrix of PNGs.
const QIcon MainWindow::getTrayIcon(const int &notificationCount) const {
  const bool monochrome = SettingsManager::instance()
                              .settings()
                              .value("monochromeTrayIcon", false)
                              .toBool();
  // The whole composition (monochrome/colour, count badge, connection dimming,
  // and the SVG→colour fallback) lives in a pure, unit-tested helper.
  //
  // Composed at several sizes rather than at 64 alone: a panel draws the tray icon
  // at about 22 px, and a two-digit badge downscaled from 64 to 22 turns to mush,
  // while the same badge drawn at 22 keeps its digits. QIcon hands the platform
  // whichever size it asks for.
  QIcon icon;
  for (const int size : {22, 24, 32, 48, 64})
    icon.addPixmap(QPixmap::fromImage(TrayIcon::composeTrayImage(
        notificationCount, monochrome, m_trayConnected, size)));
  return icon;
}

void MainWindow::handleWebViewTitleChanged(const QString &title) {
  // Which account's title changed — the signal can come from any account's
  // view, not just the visible one.
  const int idx = accountIndexForView(sender());
  if (idx < 0)
    return;

  // A title change is the signal that something arrived, and nothing more than
  // that. The number in it used to be taken as the unread count, and it is not
  // one: measured on a session with ten unread chats holding fifty-five unread
  // messages between them, WhatsApp's own title said "(1)". Whatever it counts,
  // it is neither of the two things anyone reads a badge for. So ask the page.
  //
  // The answer is chats, not messages: each chat already shows its own number in
  // the list, so a total repeats what is on screen, and a sum is dominated by
  // whichever group is busiest — which stops it answering "how many
  // conversations need me", the only question a badge is for.
  countUnread(idx);

  // Every window's title follows the account that window is showing, and a
  // detached window is not a lesser kind of window: it gets the same treatment
  // as this one, from the same string.
  if (QWidget *win = windowShowingAccount(idx))
    win->setWindowTitle(accountTitle(idx));

  refreshAccountTabs();   // per-account badge on each tab
  updateTrayUnread();     // summed badge on the single tray icon
}
