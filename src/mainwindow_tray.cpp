// Tray icon, actions, and window-title/notification-count handling.
#include "mainwindow.h"
#include "appprofile.h"
#include "common.h"

#include <algorithm>

#include <QStyleHints>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

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

// The tray icon in three independent dimensions: monochrome vs the colourful
// green (a long-standing request — the only bright icon in an otherwise
// monochrome tray), connected vs not (so a silent disconnect after boot or
// resume is visible instead of being noticed hours later), and the unread
// count. Composed here from one source rather than shipping a matrix of PNGs.
const QIcon MainWindow::getTrayIcon(const int &notificationCount) const {
  const int count = std::clamp(notificationCount, 0, 10);
  const bool monochrome = SettingsManager::instance()
                              .settings()
                              .value("monochromeTrayIcon", false)
                              .toBool();

  const int size = 64;
  QPixmap base(size, size);
  base.fill(Qt::transparent);

  if (monochrome) {
    // A monochrome tray icon is asked for so it stops being the one bright thing
    // in an otherwise grey tray — and those trays are almost always dark, with
    // the other icons light. So the glyph is tinted light rather than to the app
    // palette (which is the *window's* colour, not the panel's, and would be
    // dark and invisible under a light app theme on a dark panel). A thin dark
    // outline underneath keeps it visible on the rarer light panel too.
    // Render the SVG with QSvgRenderer rather than QIcon: QIcon's SVG path
    // silently yields nothing here (the tray then shows a broken-image glyph),
    // whereas the renderer is direct and reliable now that Qt Svg is linked.
    QImage glyphImg(size, size, QImage::Format_ARGB32_Premultiplied);
    glyphImg.fill(Qt::transparent);
    {
      QSvgRenderer renderer(QStringLiteral(":/icons/app/whatsie-symbolic.svg"));
      QPainter rp(&glyphImg);
      renderer.render(&rp);
    }
    const QPixmap mask = QPixmap::fromImage(glyphImg);

    auto tinted = [&](const QColor &c) {
      QPixmap px = mask;
      QPainter tp(&px);
      tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tp.fillRect(px.rect(), c);
      tp.end();
      return px;
    };

    const QPixmap dark = tinted(QColor(0, 0, 0, 140));   // outline halo
    const QPixmap light = tinted(QColor(0xea, 0xea, 0xea)); // main fill

    QPainter p(&base);
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        if (dx || dy)
          p.drawPixmap(dx, dy, dark);
    p.drawPixmap(0, 0, light);
    p.end();
  } else {
    // The colourful icons already carry the count badge baked in.
    const QString path =
        count == 0
            ? QStringLiteral(":/icons/app/notification/whatsie-notify.png")
            : QStringLiteral(":/icons/app/notification/whatsie-notify-%1.png")
                  .arg(count);
    QPixmap glyph(path);
    QPainter p(&base);
    p.drawPixmap(base.rect(), glyph);
  }

  // In monochrome mode the count is not baked into the glyph, so draw it.
  if (monochrome && count > 0) {
    QPainter p(&base);
    p.setRenderHint(QPainter::Antialiasing);
    const int d = 34;
    const QRect badge(size - d, 0, d, d);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xea, 0xea, 0xea)); // same light as the glyph
    p.drawEllipse(badge);
    QFont f = qApp->font();
    f.setPixelSize(count >= 10 ? 20 : 26);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x11, 0x11, 0x11)); // dark number on the light badge
    p.drawText(badge, Qt::AlignCenter, count >= 10 ? QStringLiteral("9+")
                                                   : QString::number(count));
    p.end();
  }


  // Not connected: dim the whole thing so it plainly reads as inactive.
  if (!m_trayConnected) {
    QPixmap dimmed(size, size);
    dimmed.fill(Qt::transparent);
    QPainter p(&dimmed);
    p.setOpacity(0.40);
    p.drawPixmap(0, 0, base);
    p.end();
    return QIcon(dimmed);
  }

  return QIcon(base);
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
