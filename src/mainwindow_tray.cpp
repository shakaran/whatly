// Tray icon, actions, and window-title/notification-count handling.
#include "mainwindow.h"
#include "appprofile.h"
#include "common.h"

#include <algorithm>

#include <QStyleHints>

// ── Actions ──────────────────────────────────────────────────────────────────

void MainWindow::createActions() {
  m_openUrlAction = new QAction("New Chat", this);
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
  connect(m_minimizeAction, &QAction::triggered, this, &QMainWindow::hide);
  addAction(m_minimizeAction);

  m_restoreAction = new QAction(tr("&Restore"), this);
  connect(m_restoreAction, &QAction::triggered, this, &QMainWindow::show);
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

  m_settingsAction = new QAction(tr("&Settings"), this);
  m_settingsAction->setShortcut(QKeySequence(Qt::Modifier::CTRL | Qt::Key_P));
  connect(m_settingsAction, &QAction::triggered, this,
          &MainWindow::showSettings);
  addAction(m_settingsAction);

  m_toggleThemeAction = new QAction(tr("&Toggle theme"), this);
  m_toggleThemeAction->setShortcut(
      QKeySequence(Qt::Modifier::CTRL | Qt::Key_T));
  connect(m_toggleThemeAction, &QAction::triggered, this,
          &MainWindow::toggleTheme);
  addAction(m_toggleThemeAction);

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
}

// ── Tray icon ─────────────────────────────────────────────────────────────────

void MainWindow::createTrayIcon() {
  m_trayIconMenu = new QMenu(this);
  m_trayIconMenu->setObjectName("trayIconMenu");
  m_trayIconMenu->addAction(m_minimizeAction);
  m_trayIconMenu->addAction(m_restoreAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_reloadAction);
  m_trayIconMenu->addAction(m_lockAction);
  m_trayIconMenu->addAction(m_muteAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_openUrlAction);
  m_trayIconMenu->addAction(m_toggleThemeAction);
  m_trayIconMenu->addAction(m_settingsAction);
  m_trayIconMenu->addAction(m_aboutAction);
  m_trayIconMenu->addSeparator();
  m_trayIconMenu->addAction(m_quitAction);

  m_systemTrayIcon = new QSystemTrayIcon(m_trayIconNormal, this);
  m_systemTrayIcon->setContextMenu(m_trayIconMenu);
  connect(m_trayIconMenu, &QMenu::aboutToShow, this,
          &MainWindow::checkWindowState);
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
  const bool visible = isVisible();
  if (m_minimizeAction)
    m_minimizeAction->setEnabled(visible);
  if (m_restoreAction)
    m_restoreAction->setEnabled(true);
  if (m_lockAction)
    m_lockAction->setEnabled(!(m_lockWidget && m_lockWidget->getIsLocked()));
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
  if (SettingsManager::instance()
              .settings()
              .value("minimizeOnTrayIconClick", false)
              .toBool() == false ||
      reason == QSystemTrayIcon::Context)
    return;
  if (isVisible()) {
    hide();
  } else {
    show();
  }
}

const QIcon MainWindow::getTrayIcon(const int &notificationCount) const {
  if (notificationCount == 0)
    return themeIcon("whatsie-tray", ":/icons/app/notification/whatsie-notify.png");

  return themeIcon("whatsie-tray-attentions",
    QString(":/icons/app/notification/whatsie-notify-%1.png").arg(std::clamp(notificationCount, 1, 10)));
}

void MainWindow::handleWebViewTitleChanged(const QString &title) {
  // Which account's title changed — the signal can come from any account's
  // view, not just the visible one.
  const int idx = accountIndexForView(sender());
  if (idx < 0)
    return;

  // Pull the unread count out of the title ("(3) Chat name"), and remember it
  // per account so the tray can show the total across all of them.
  int unread = 0;
  const QRegularExpressionMatch titleMatch =
      m_notificationsTitleRegExp.match(title);
  if (titleMatch.hasMatch()) {
    const QRegularExpressionMatch countMatch =
        m_unreadMessageCountRegExp.match(titleMatch.captured(0));
    if (countMatch.hasMatch())
      unread = countMatch.captured(1).toInt();
  }
  m_accounts[idx].unread = unread;

  // The window title follows the active account only.
  if (idx == m_activeAccount)
    setWindowTitle(QApplication::applicationName() + AppProfile::label() +
                   ": " + title);

  refreshAccountTabs();   // per-account badge on each tab
  updateTrayUnread();     // summed badge on the single tray icon
}
