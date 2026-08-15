// Core MainWindow: constructor, lifecycle events, settings UI, notifications,
// zoom, and navigation helpers.
// WebEngine, tray, and lock logic live in mainwindow_webengine/tray/lock.cpp.
#include "mainwindow.h"
#include "appprofile.h"

#include <algorithm>
#include <QInputDialog>
#include <QRegularExpression>
#include <QScreen>
#include <QSessionManager>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStyleHints>
#include <QUrlQuery>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#endif

#include "about.h"
#include "common.h"
#include "globalshortcut.h"
#include "quickcompose.h"
#include "linkeddevicename.h"
#include "rateapp.h"
#include "theme.h"
#include "chattheme.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "customjs.h"
#include "setupwizard.h"
#include "customtitlebar.h"
#include "updatechecker.h"
#include "quickreply.h"
#include "focusmode.h"
#include "hdmedia.h"
#include "undosend.h"

#include <QTimer>
#include <QDesktopServices>
#include <QMessageBox>
#include <QProcess>
#include <QVarLengthArray>
#ifdef Q_OS_UNIX
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "chatliststrip.h"
#include "chatnav.h"
#include "privacyblur.h"
#include "localapi.h"
#include "cloudapi.h"
#include "autoreply.h"
#include "webfont.h"
#include "mutedstatus.h"
#include "scheduledmessages.h"
#include "detachedaccountwindow.h"
#include "scheduledmessagesdialog.h"
#include "webengineprofilemanager.h"
#include "webtweaks.h"
#include "webview.h"

extern double defaultZoomFactorMaximized;
extern int    defaultAppAutoLockDuration;
extern bool   defaultAppAutoLock;

// ── Constructor / destructor ──────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
#ifdef Q_OS_LINUX
      m_notifier(kAppDisplayName, this),
#endif
      m_trayIconNormal(themeIcon("whatly-tray", ":/icons/app/notification/whatly-notify.png")) {

  setObjectName("MainWindow");
  // No application name in here: Qt appends it to every window title at the
  // platform layer, so a title that carried one too would read "Whatly … Whatly"
  // in the system's title bar, the task list and Alt-Tab alike. What a bar of
  // ours writes is CustomTitleBar::barTitle(), which puts the name back exactly
  // once, because there no platform is going to.
  setWindowTitle(AppProfile::label().trimmed());
  setWindowIcon(themeIcon("whatly", ":/icons/app/icon-64.png"));
  // Optional client-side decoration: drop the native frame so buildAccountArea
  // can add its own title bar. Off by default, so nothing changes for anyone who
  // has not opted in.
  if (CustomTitleBar::isEnabled())
    setWindowFlag(Qt::FramelessWindowHint, true);
  applyMinimumSize();
  restoreMainWindow();
  createActions();
  createTrayIcon();

  // Scheduled messages: the manager persists and times the queue; when a
  // message comes due it asks here to drive the page. Created before the web
  // engine so installPageBridge can wire the result callback to it.
  m_scheduledMessages = new ScheduledMessages(this);
  connect(m_scheduledMessages, &ScheduledMessages::sendRequested, this,
          [this](const QString &id, const QString &number, const QString &text) {
            if (m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(
                  ScheduledMessages::startJobScript(id, number, text));
            else
              m_scheduledMessages->reportResult(
                  id, false, tr("No WhatsApp window is open"));
          });
  connect(m_scheduledMessages, &ScheduledMessages::reminderDue, this,
          [this](const QString &, const QString &name, const QString &text) {
            const QString title =
                name.isEmpty() ? tr("Reminder") : tr("Reminder: %1").arg(name);
            if (m_systemTrayIcon && QSystemTrayIcon::supportsMessages())
              m_systemTrayIcon->showMessage(title, text, windowIcon(), 15000);
            // A reply reminder (snooze a chat) names the chat: reopen it so it is
            // waiting when the user returns. A no-op if that chat is not listed.
            if (!name.isEmpty() && m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(
                  ChatNav::openChatByNameScript(name));
          });

  createWebEngine();
  initSettingWidget();
  initRateWidget();
  QApplication::processEvents();
  tryLock();
  updateWindowTheme();
  initAutoLock();

  // Warn (once, shortly after the window is up) if the data folder's disk is
  // nearly full — a truncated LevelDB write there corrupts WhatsApp Web's
  // storage and forces a re-link. The warning offers to move the folder.
  QTimer::singleShot(2000, this, &MainWindow::checkStorageSpace);

  // Local HTTP API (opt-in, loopback only): lets other programs on this machine
  // send through the running instance, the HTTP counterpart of `whatly --send`.
  m_localApi = new LocalApiServer(this);
  connect(m_localApi, &LocalApiServer::sendRequested, this,
          &MainWindow::commandSend);
  connect(m_localApi, &LocalApiServer::webhookMessageReceived, this,
          &MainWindow::handleCloudIncoming);
  startLocalApi();

  // Follow the desktop's light/dark preference, live, when the setting is on.
  // The portal's SettingChanged signal is what actually fires on GNOME (Qt's
  // colorSchemeChanged does not here); keep the Qt signal too for desktops where
  // it is the one that works.
#ifdef Q_OS_LINUX
  QDBusConnection::sessionBus().connect(
      QStringLiteral("org.freedesktop.portal.Desktop"),
      QStringLiteral("/org/freedesktop/portal/desktop"),
      QStringLiteral("org.freedesktop.portal.Settings"),
      QStringLiteral("SettingChanged"), this,
      SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));

  // Lock Whatly when the desktop session locks (freedesktop + GNOME savers).
  for (const QString &iface : {QStringLiteral("org.freedesktop.ScreenSaver"),
                               QStringLiteral("org.gnome.ScreenSaver")}) {
    QDBusConnection::sessionBus().connect(
        QString(), QString(), iface, QStringLiteral("ActiveChanged"), this,
        SLOT(onScreenSaverActiveChanged(bool)));
  }
#endif
  connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme) { applySystemThemeIfEnabled(); });
  applySystemThemeIfEnabled();

  // When the desktop session is ending (log out, reboot, shutdown) the window
  // gets a close event just like the user pressing X — and minimize-to-tray
  // used to veto it, which the session manager reads as "this app refused to
  // close" and stalls the logout (reported on KDE). Treat a session-manager
  // close as a real quit: mark it so closeEvent accepts instead of hiding.
  connect(qApp, &QGuiApplication::commitDataRequest, this,
          [this](QSessionManager &) { m_isQuitting = true; });

#ifdef Q_OS_LINUX
  // System-wide Ctrl+Alt+W to bring the window to the front from anywhere. It
  // goes through the desktop portal (which works on Wayland and X11), falling
  // back to a raw X11 grab; if neither is available, a `whatly -w` desktop
  // shortcut is the alternative.
  m_globalShortcut = new GlobalShortcut(this);
  if (m_globalShortcut->tryRegister()) {
    connect(m_globalShortcut, &GlobalShortcut::activated, this,
            [this](const QString &id) {
              if (id == QLatin1String("quick-compose")) {
                showQuickCompose();
                return;
              }
              // The last window used, not this one: the shortcut means "show me
              // Whatly", and which window happens to own the tray icon is not
              // something the user should be made aware of.
              bringForward(frontWindow());
            });
  } else {
    qInfo() << "No global-shortcut backend available; bind a desktop shortcut "
               "to `whatly -w` to raise the window.";
  }
#endif

  // Once-a-day update check (opt-out), deferred so it never delays startup.
  m_updateChecker = new UpdateChecker(this);
  connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
          [this](const QString &version, const QString &url) {
            // What to advise depends on who owns the update. Telling someone on
            // a Flatpak or a distribution package to go and download a build is
            // wrong: their package manager does it, and a sandboxed application
            // cannot replace itself anyway. The click still opens the release
            // page in every case, because the notes are worth reading wherever
            // the new version will come from.
            QString advice;
            switch (UpdateCheck::currentInstall()) {
            case UpdateCheck::Install::Flatpak:
              advice = tr("Whatly %1 is available. Update it through Flathub or "
                          "your software centre.");
              break;
            case UpdateCheck::Install::DistroPackage:
              advice =
                  tr("Whatly %1 is available. Update it with your package "
                     "manager.");
              break;
            case UpdateCheck::Install::AppImage:
              advice = tr("Whatly %1 is available. This AppImage can update "
                          "itself in place with AppImageUpdate, fetching only "
                          "the parts that changed.");
              break;
            case UpdateCheck::Install::Unknown:
              // Kept word for word: it already has its translations.
              advice =
                  tr("Whatly %1 is available. Click to open the download page.");
              break;
            }
#ifdef Q_OS_LINUX
            // Route through libnotify with a registered "open" action, exactly
            // like message notifications do. QSystemTrayIcon::showMessage
            // registers no action, so on KDE (and any freedesktop server) a
            // click on the notification body invokes nothing and does nothing —
            // that was issue #74. libnotify's actionInvoked does fire. This also
            // avoids the tray messageClicked path that the code itself warns
            // against on Linux (see mainwindow_tray.cpp).
            auto ntf = m_notifier.createNotification(tr("Update available"),
                                                     advice.arg(version), kAppId);
            ntf->setTimeout(15000);
            ntf->addAction("open", tr("Open"));
            ntf->setHintString("image-path", kAppId);
            ntf->setHintString("desktop-entry", kAppId);
            ntf->setHint("image-data",
                         notificationImageHint(QPixmap(":/icons/app/icon-256.png")));
            const QString openUrl = url;
            QObject::connect(ntf.get(), &Notification::Event::actionInvoked, this,
                             [openUrl](const QString &action) {
                               if (action == "open")
                                 QDesktopServices::openUrl(QUrl(openUrl));
                             });
            ntf->show();
#else
            if (m_systemTrayIcon && QSystemTrayIcon::supportsMessages())
              m_systemTrayIcon->showMessage(tr("Update available"),
                                            advice.arg(version), windowIcon(),
                                            15000);
            m_pendingUpdateUrl = url;
#endif
          });
#ifndef Q_OS_LINUX
  // Windows/macOS: the tray toast has no action buttons, so a click on the body
  // (QSystemTrayIcon::messageClicked) is how the release page is opened. On
  // Linux this path is deliberately not used — see the libnotify branch above.
  connect(m_systemTrayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
    if (!m_pendingUpdateUrl.isEmpty()) {
      QDesktopServices::openUrl(QUrl(m_pendingUpdateUrl));
      m_pendingUpdateUrl.clear();
    }
  });
#endif
  QTimer::singleShot(4000, this, [this]() { m_updateChecker->check(false); });

  // First-run wizard: shown once, after the window is up, and only when it has
  // not been completed for this account. Deferred to the event loop so the main
  // window paints behind it first.
  if (!SetupWizard::isCompleted()) {
    QTimer::singleShot(0, this, [this]() {
      auto *wizard = new SetupWizard(this);
      wizard->setAttribute(Qt::WA_DeleteOnClose);
      // Mark completed even if dismissed, so it does not reappear every launch.
      connect(wizard, &QDialog::rejected, this, []() {
        SetupWizard::markCompleted();
      });
      wizard->show();
    });
  }
}

MainWindow::~MainWindow() { m_webEngine->deleteLater(); }

// ── Window geometry ───────────────────────────────────────────────────────────

// Qt's saveGeometry() is not usable while the window is maximized: on Wayland
// normalGeometry() then reports the *maximized* rectangle, so the blob records
// "maximized, and the size to restore down to is the maximized size". Restoring
// that on the next run left the window maximized from the very first frame,
// having never been in a normal state, so the compositor — which decides the
// restore-down size on Wayland — had nothing to go back to and the button did
// nothing. Track the normal geometry ourselves instead, and on startup show the
// window normal first and maximize afterwards, so the compositor learns the
// size to restore to. (Reproduced with a bare QMainWindow: it is a Qt/Wayland
// behaviour, not something this app causes.)
void MainWindow::trackNormalGeometry() {
  if (!isVisible() || isMaximized() || isFullScreen() || isMinimized())
    return;

  // On Wayland the resize to the maximized size arrives *before* the maximized
  // flag flips, so right now the window can look like a normal window that
  // happens to be screen-sized — and recording that would store the maximized
  // rectangle as the normal one, the very corruption this exists to avoid. Let
  // the geometry settle and check the state again before believing it.
  if (!m_normalGeometryTimer) {
    m_normalGeometryTimer = new QTimer(this);
    m_normalGeometryTimer->setSingleShot(true);
    m_normalGeometryTimer->setInterval(250);
    connect(m_normalGeometryTimer, &QTimer::timeout, this, [this]() {
      if (isVisible() && !isMaximized() && !isFullScreen() && !isMinimized())
        m_normalGeometry = geometry();
    });
  }
  m_normalGeometryTimer->start();
}

void MainWindow::saveWindowGeometry() {
  QSettings &settings = SettingsManager::instance().settings();
  const bool maximized = isMaximized() || isFullScreen();

  // Last line of defence: never persist the maximized rectangle as the normal
  // geometry. If it slipped through anyway, keep whatever was stored before
  // rather than write a value that leaves restore-down with nowhere to go.
  const bool looksLikeTheMaximizedRect = maximized && m_normalGeometry == geometry();
  if (m_normalGeometry.isValid() && !looksLikeTheMaximizedRect)
    settings.setValue("normalGeometry", m_normalGeometry);

  settings.setValue("wasMaximized", maximized);
}

void MainWindow::restoreMainWindow() {
  QSettings &settings = SettingsManager::instance().settings();

  const QRect normalGeometry = settings.value("normalGeometry").toRect();
  if (normalGeometry.isValid()) {
    m_normalGeometry = normalGeometry;
    setGeometry(normalGeometry);
    // Applied once the window is on screen, not here: it has to be mapped in
    // its normal state first for the restore-down size to be remembered.
    m_restoreMaximized = settings.value("wasMaximized", false).toBool();
    return;
  }

  // Installs that predate the keys above still have Qt's blob.
  if (settings.value("geometry").isValid()) {
    restoreGeometry(settings.value("geometry").toByteArray());

    if (isMaximized() || isFullScreen()) {
      // A blob saved while maximized carries the maximized rectangle as its
      // normal geometry, so there is no previous size left in it to recover.
      // Come up normal at the default size and maximize after mapping: without
      // this the window would stay stuck maximized, and — never having been in
      // a normal state — would never record a normal geometry to save either,
      // so it could not heal on its own.
      setWindowState(windowState() &
                     ~(Qt::WindowMaximized | Qt::WindowFullScreen));
      resize(800, 684);
      m_restoreMaximized = true;
    }

    if (!m_restoreMaximized) {
      QPoint pos = QCursor::pos();
      for (auto screen : QGuiApplication::screens()) {
        QRect screenRect = screen->geometry();
        if (screenRect.contains(pos)) {
          move(screenRect.center() - rect().center());
        }
      }
    }
    m_normalGeometry = geometry();
  } else {
    resize(800, 684);
  }
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);

  // Refresh the tray menu here as well as from its aboutToShow signal: a menu
  // the desktop shell renders itself may never emit that, leaving the entries
  // stale (see checkWindowState).
  checkWindowState();
  // Back from the tray: whatever was unloaded with this window is built again.
  noteWindowVisibilityChanged();

  if (m_geometryRestored)
    return;
  m_geometryRestored = true;
  if (!m_restoreMaximized)
    return;
  // Queued, so the window is actually mapped at its normal size before the
  // compositor is asked to maximize it.
  QTimer::singleShot(0, this, [this]() { showMaximized(); });
}

void MainWindow::hideEvent(QHideEvent *event) {
  QMainWindow::hideEvent(event);
  checkWindowState();
  noteWindowVisibilityChanged(); // put away to the tray: start the clock on it
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  trackNormalGeometry();
  // A user resize while the grid is showing counts as a customization (ours are
  // flagged via m_gridResizing so they don't).
  if (m_viewMode == ViewMode::Grid && !m_gridResizing &&
      event->oldSize().isValid() && event->size() != event->oldSize())
    markGridCustomized();
  if (!m_lockWidget || event->size() == event->oldSize())
    return;
  // Track the central widget it now lives in, not the whole window.
  m_lockWidget->resize(centralWidget() ? centralWidget()->size() : size());
}

void MainWindow::moveEvent(QMoveEvent *event) {
  QMainWindow::moveEvent(event);
  trackNormalGeometry();
}

// ── Window state & zoom ───────────────────────────────────────────────────────

void MainWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::WindowStateChange) {
    handleZoomOnWindowStateChange(static_cast<QWindowStateChangeEvent *>(e));
    // Minimised or restored: both directions land here, and the handler works out
    // which it was from the state the window is in now. A hide has its own event
    // — Qt does not call minimising a state change of visibility.
    noteWindowVisibilityChanged();
  }
  // Remember when the window last lost activation: a tray-icon click moves
  // focus to the shell before iconActivated() runs, so this lets it tell "was
  // frontmost a moment ago" apart from "buried under another window".
  if (e->type() == QEvent::ActivationChange && !isActiveWindow())
    m_lastDeactivationMs = QDateTime::currentMSecsSinceEpoch();
  // Track focus order: the main window becoming active makes it "main" (front).
  if (e->type() == QEvent::ActivationChange && isActiveWindow())
    noteWindowFocused(nullptr);
  QMainWindow::changeEvent(e);
}

void MainWindow::handleZoomOnWindowStateChange(
    const QWindowStateChangeEvent *ev) {
  if (m_settingsWidget == nullptr)
    return;
  if (ev->oldState().testFlag(Qt::WindowMaximized) &&
      windowState().testFlag(Qt::WindowNoState)) {
    emit m_settingsWidget->zoomChanged();
  } else if ((!ev->oldState().testFlag(Qt::WindowMaximized) &&
              windowState().testFlag(Qt::WindowMaximized)) ||
             (!ev->oldState().testFlag(Qt::WindowMaximized) &&
              windowState().testFlag(Qt::WindowFullScreen))) {
    emit m_settingsWidget->zoomMaximizedChanged();
  }
}

// The minimum window size follows the normal zoom factor. At a zoom below 1 the
// content is smaller, so the window should be allowed to shrink with it —
// otherwise a user who zooms out to tuck the window into a corner cannot resize
// it down to match, which is the whole point of zooming out. A zoom above 1 does
// not force a larger minimum: WhatsApp Web reflows, so the base size still fits.
void MainWindow::applyMinimumSize() {
  const double zoom = SettingsManager::instance()
                          .settings()
                          .value("zoomFactor", 1.0)
                          .toDouble();
  const double factor = std::clamp(zoom, 0.5, 1.0);
  int minW = static_cast<int>(kBaseMinWidth * factor);
  int minH = static_cast<int>(kBaseMinHeight * factor);
  // On a small display — a Linux phone such as the PinePhone in portrait, or any
  // cramped screen — the base minimum can be wider or taller than the screen
  // itself, which pins the window larger than it can fit and clips the UI. Never
  // demand more than the available screen area (issue #239).
  if (QScreen *scr = screen()) {
    const QSize avail = scr->availableSize();
    minW = std::min(minW, avail.width());
    minH = std::min(minH, avail.height());
  }
  setMinimumWidth(minW);
  setMinimumHeight(minH);
}

void MainWindow::handleZoom() {
  if (windowState().testFlag(Qt::WindowMaximized) ||
      windowState().testFlag(Qt::WindowFullScreen)) {
    double currentFactor =
        SettingsManager::instance()
            .settings()
            .value("zoomFactorMaximized", defaultZoomFactorMaximized)
            .toDouble();
    m_webEngine->page()->setZoomFactor(currentFactor);
  } else if (windowState().testFlag(Qt::WindowNoState)) {
    double currentFactor = SettingsManager::instance()
                               .settings()
                               .value("zoomFactor", 1.0)
                               .toDouble();
    m_webEngine->page()->setZoomFactor(currentFactor);
    applyMinimumSize();   // let the window shrink to match a zoomed-out page
  }
}

// Ctrl +/-/0 zoom. The page keeps two zoom levels (normal and maximized), so
// nudge whichever one is in effect, persist it, and re-apply.
void MainWindow::zoomBy(double delta) {
  const bool maximized = windowState().testFlag(Qt::WindowMaximized) ||
                         windowState().testFlag(Qt::WindowFullScreen);
  const QString key = maximized ? QStringLiteral("zoomFactorMaximized")
                                : QStringLiteral("zoomFactor");
  const double def = maximized ? defaultZoomFactorMaximized : 1.0;
  QSettings &s = SettingsManager::instance().settings();
  const double next = clampZoom(s.value(key, def).toDouble() + delta);
  s.setValue(key, next);
  m_webEngine->page()->setZoomFactor(next);
  applyMinimumSize();
}

void MainWindow::zoomIn() { zoomBy(0.1); }
void MainWindow::zoomOut() { zoomBy(-0.1); }

// Reset the in-effect zoom (normal or maximized) back to 1:1.
void MainWindow::zoomReset() {
  // In grid view, Ctrl+0 redistributes the tiles equally instead of zooming.
  if (m_viewMode == ViewMode::Grid) {
    resetGridTiles();
    return;
  }
  const bool maximized = windowState().testFlag(Qt::WindowMaximized) ||
                         windowState().testFlag(Qt::WindowFullScreen);
  const QString key = maximized ? QStringLiteral("zoomFactorMaximized")
                                : QStringLiteral("zoomFactor");
  SettingsManager::instance().settings().setValue(
      key, maximized ? defaultZoomFactorMaximized : 1.0);
  handleZoom();
}

// ── Theme ─────────────────────────────────────────────────────────────────────

// Follow the desktop's own light/dark preference (GNOME's colour-scheme toggle,
// KDE's, the freedesktop appearance portal — Qt surfaces them all through
// QStyleHints::colorScheme). Enabled by a setting; when on, it overrides the
// manual theme and the sunrise/sunset switcher, and re-applies the moment the
// system preference changes.
// The desktop's light/dark preference, as "dark"/"light", or empty when it
// cannot be determined. The freedesktop appearance portal is the source of
// truth and works on GNOME and KDE alike (color-scheme: 1 = dark, 2 = light);
// QStyleHints is only a fallback, because on GNOME without a Qt platform theme
// it does not track the setting at all (measured: it stayed "Light" with the
// system in dark).
static QString desktopColorScheme() {
#ifdef Q_OS_LINUX
  QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                        QStringLiteral("/org/freedesktop/portal/desktop"),
                        QStringLiteral("org.freedesktop.portal.Settings"),
                        QDBusConnection::sessionBus());
  if (portal.isValid()) {
    // portal Settings.Read returns a variant that GNOME nests inside another
    // variant, so unwrap QDBusVariant until a plain value is left — otherwise
    // every read looks empty and the whole thing silently falls back to Qt
    // (which does not track the setting here at all).
    const auto read = [&](const QString &ns) -> QVariant {
      QDBusReply<QVariant> reply = portal.call(
          QStringLiteral("Read"), ns, QStringLiteral("color-scheme"));
      if (!reply.isValid())
        return QVariant();
      QVariant v = reply.value();
      while (v.canConvert<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
      return v;
    };

    // GNOME's own key first, as a string. The standard appearance namespace is
    // stuck reporting "light" on at least one GNOME here (measured), while this
    // one tracks the setting correctly. Reading through the portal, not
    // gsettings, keeps it working inside a flatpak/snap sandbox.
    const QString gnome = read(QStringLiteral("org.gnome.desktop.interface")).toString();
    if (gnome == QLatin1String("prefer-dark"))
      return QStringLiteral("dark");
    if (gnome == QLatin1String("prefer-light"))
      return QStringLiteral("light");
    // "default" or absent → try the standard namespace.

    // The cross-desktop standard (KDE and a healthy GNOME): 1 = dark, 2 = light.
    const QVariant fdo = read(QStringLiteral("org.freedesktop.appearance"));
    if (fdo.isValid()) {
      const uint scheme = fdo.toUInt();
      if (scheme == 1)
        return QStringLiteral("dark");
      if (scheme == 2)
        return QStringLiteral("light");
    }
  }
#endif
  switch (qApp->styleHints()->colorScheme()) {
  case Qt::ColorScheme::Dark:
    return QStringLiteral("dark");
  case Qt::ColorScheme::Light:
    return QStringLiteral("light");
  default:
    return QString();
  }
}

void MainWindow::applySystemThemeIfEnabled() {
  if (!SettingsManager::instance()
           .settings()
           .value("followSystemTheme", false)
           .toBool())
    return;

  const QString theme = desktopColorScheme();
  if (theme.isEmpty()) // no preference exposed; leave the theme as it is
    return;
  if (SettingsManager::instance().settings().value("windowTheme").toString() ==
      theme)
    return;

  SettingsManager::instance().settings().setValue("windowTheme", theme);
  updateWindowTheme();
  updatePageTheme();
  if (m_settingsWidget)
    m_settingsWidget->refresh();   // keep the theme combo in step
}

#ifdef Q_OS_LINUX
void MainWindow::onPortalSettingChanged(const QString &nspace,
                                        const QString &key,
                                        const QDBusVariant &value) {
  Q_UNUSED(value);
  // Either namespace's color-scheme change is worth re-checking — GNOME emits on
  // org.gnome.desktop.interface, KDE on org.freedesktop.appearance.
  if (key == QLatin1String("color-scheme") &&
      (nspace == QLatin1String("org.freedesktop.appearance") ||
       nspace == QLatin1String("org.gnome.desktop.interface")))
    applySystemThemeIfEnabled();
}
#endif

void MainWindow::updateWindowTheme() {
  qApp->setStyle(QStyleFactory::create(SettingsManager::instance()
                                           .settings()
                                           .value("widgetStyle", "Fusion")
                                           .toString()));
  const bool dark = SettingsManager::instance()
                        .settings()
                        .value("windowTheme", "light")
                        .toString() == "dark";
  qApp->setPalette(dark ? Theme::getDarkPalette() : Theme::getLightPalette());
  const QString viewStyle =
      dark ? QStringLiteral("QWebEngineView{background:rgb(17, 27, 33);}")
           : QStringLiteral("QWebEngineView{background:#F0F0F0;}");
  const QColor pageBg = dark ? QColor(17, 27, 33) : QColor(240, 240, 240);

  m_webEngine->setStyleSheet(viewStyle);
  if (m_webEngine->page())
    m_webEngine->page()->setBackgroundColor(pageBg);

  // And every other account: in grid view they are all on screen at once, so a
  // theme change that reached only the current one left the rest of the tiles
  // sitting on the old background.
  for (const Account &account : m_accounts) {
    if (!account.view || account.view == m_webEngine)
      continue;
    account.view->setStyleSheet(viewStyle);
    if (pageOf(account))
      pageOf(account)->setBackgroundColor(pageBg);
  }

  for (QWidget *w : findChildren<QWidget *>())
    w->setPalette(qApp->palette());

  setNotificationPresenter(m_webEngine->page()->profile());

  if (m_lockWidget != nullptr) {
    m_lockWidget->setStyleSheet(
        "QWidget#login{background-color:palette(window)};"
        "QWidget#signup{background-color:palette(window)};");
    m_lockWidget->applyThemeQuirks();
  }
  update();
}

// ── Settings widget ───────────────────────────────────────────────────────────

void MainWindow::initSettingWidget() {
  int screenNumber = qApp->screens().indexOf(screen());
  if (m_settingsWidget != nullptr)
    return;

  m_settingsWidget = new SettingsWidget(
      this, screenNumber, m_webEngine->page()->profile()->cachePath(),
      m_webEngine->page()->profile()->persistentStoragePath());
  m_settingsWidget->setWindowTitle(QApplication::applicationDisplayName() +
                                   " | Settings");
  m_settingsWidget->setWindowFlags(Qt::Dialog);

  connect(m_settingsWidget, &SettingsWidget::initLock, this,
          &MainWindow::initLock);
  connect(m_settingsWidget, &SettingsWidget::changeLockPassword, this,
          &MainWindow::changeLockPassword);
  connect(m_settingsWidget, &SettingsWidget::appAutoLockChanged, this,
          &MainWindow::appAutoLockChanged);
  connect(m_settingsWidget, &SettingsWidget::updateWindowTheme, this,
          &MainWindow::updateWindowTheme);
  connect(m_settingsWidget, &SettingsWidget::updatePageTheme, this,
          &MainWindow::updatePageTheme);
  connect(m_settingsWidget, &SettingsWidget::muteToggled, this,
          &MainWindow::toggleMute);
  connect(m_settingsWidget, &SettingsWidget::localApiSettingsChanged, this,
          &MainWindow::startLocalApi);

  connect(m_settingsWidget, &SettingsWidget::userAgentChanged,
          m_settingsWidget, [=](QString userAgentStr) {
            if (m_webEngine->page()->profile()->httpUserAgent() !=
                userAgentStr) {
              SettingsManager::instance().settings().setValue("useragent",
                                                              userAgentStr);
              updateSettingsUserAgentWidget();
              askToReloadPage();
            }
          });

  connect(m_settingsWidget, &SettingsWidget::autoPlayMediaToggled,
          m_settingsWidget, [=](bool checked) {
            WebEngineProfileManager::instance().profile()->settings()
                ->setAttribute(
                    QWebEngineSettings::PlaybackRequiresUserGesture, checked);
          });

  connect(m_settingsWidget, &SettingsWidget::zoomChanged, m_settingsWidget,
          [=]() {
            if (windowState() == Qt::WindowNoState ||
                !(windowState() & Qt::WindowMaximized)) {
              double currentFactor = SettingsManager::instance()
                                         .settings()
                                         .value("zoomFactor", 1.0)
                                         .toDouble();
              m_webEngine->page()->setZoomFactor(currentFactor);
            }
          });

  connect(m_settingsWidget, &SettingsWidget::zoomMaximizedChanged,
          m_settingsWidget, [=]() {
            if (windowState() & Qt::WindowMaximized ||
                windowState() & Qt::WindowFullScreen) {
              double currentFactor =
                  SettingsManager::instance()
                      .settings()
                      .value("zoomFactorMaximized", defaultZoomFactorMaximized)
                      .toDouble();
              m_webEngine->page()->setZoomFactor(currentFactor);
            }
          });

  connect(m_settingsWidget, &SettingsWidget::notificationPopupTimeOutChanged,
          m_settingsWidget, [=]() {
            setNotificationPresenter(m_webEngine->page()->profile());
          });

  connect(m_settingsWidget, &SettingsWidget::restartRequested, this,
          &MainWindow::restartApp);

  // Coming back from a restart: put the settings page back the way it was left,
  // then forget it, so an ordinary launch does not open it.
  {
    QSettings &s = SettingsManager::instance().settings();
    if (s.value(QStringLiteral("ui/settingsWasOpen"), false).toBool()) {
      s.setValue(QStringLiteral("ui/settingsWasOpen"), false);
      QTimer::singleShot(0, this, [this]() {
        showSettings();
        if (m_settingsWidget)
          m_settingsWidget->restoreUiState();
      });
    }
  }

  connect(m_settingsWidget, &SettingsWidget::webTweaksChanged, m_settingsWidget,
          [=]() {
            // Update the profile scripts for future page loads, and apply the
            // change to the already-loaded pages (Qt does not propagate profile
            // script changes to an existing page). Every account, not just the
            // current one: these buttons are drawn inside each account's page,
            // and in grid view they are all on screen at once.
            WebEngineProfileManager::instance().applyUserSettings();
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(WebTweaks::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::followSystemThemeChanged,
          m_settingsWidget, [=]() { applySystemThemeIfEnabled(); });

  connect(m_settingsWidget, &SettingsWidget::trayIconChanged, this, [this]() {
    const bool hidden = SettingsManager::instance()
                            .settings()
                            .value("hideTrayIcon", false)
                            .toBool();
    m_systemTrayIcon->setVisible(!hidden);
    if (!hidden)
      updateTrayUnread();
  });

  connect(m_settingsWidget, &SettingsWidget::customCssChanged, m_settingsWidget,
          [=]() {
            CustomCss::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(CustomCss::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::focusModeChanged, m_settingsWidget,
          [=]() {
            FocusMode::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(FocusMode::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::hdMediaChanged, m_settingsWidget,
          [=]() {
            HdMedia::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(HdMedia::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::undoSendChanged, m_settingsWidget,
          [=]() {
            UndoSend::install(WebEngineProfileManager::instance().profile());
            // The live script self-retunes from its own state when re-run, so
            // enabling/disabling or changing the delay takes effect without a
            // reload.
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(UndoSend::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::customJsChanged, m_settingsWidget,
          [=]() {
            // Reinstall for future loads; a full effect for script addons needs
            // a reload, so this only refreshes the injected collection.
            CustomJs::install(WebEngineProfileManager::instance().profile());
          });

  connect(m_settingsWidget, &SettingsWidget::chatWallpaperChanged,
          m_settingsWidget, [=]() {
            ChatWallpaper::install(WebEngineProfileManager::instance().profile());
            if (m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(ChatWallpaper::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::chatThemeChanged, m_settingsWidget,
          [=]() {
            ChatTheme::install(WebEngineProfileManager::instance().profile());
            if (!m_webEngine || !m_webEngine->page())
              return;
            const QString js = ChatTheme::scriptSource();
            // "none" injects nothing, so the live page needs to be told to drop
            // the stylesheet a previous theme left behind.
            m_webEngine->page()->runJavaScript(
                js.isEmpty()
                    ? QStringLiteral("(function(){var e=document.getElementById("
                                     "'whatly-chat-theme'); if (e) e.remove();"
                                     "window.__whatlyChatThemeApply = null;})();")
                    : js);
          });

  connect(m_settingsWidget, &SettingsWidget::privacyBlurChanged,
          m_settingsWidget, [=]() {
            WebEngineProfileManager::instance().applyUserSettings();
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(
                    PrivacyBlur::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::chatListStripChanged,
          m_settingsWidget, [=]() {
            WebEngineProfileManager::instance().applyUserSettings();
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(
                    ChatListStrip::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::fontChanged, m_settingsWidget,
          [=]() {
            WebFont::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(WebFont::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::mutedStatusChanged,
          m_settingsWidget, [=]() {
            MutedStatus::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (pageOf(account))
                pageOf(account)->runJavaScript(MutedStatus::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::spellCheckChanged, m_settingsWidget,
          [=]() { WebEngineProfileManager::instance().applyUserSettings(); });

  connect(m_settingsWidget, &SettingsWidget::notify, m_settingsWidget,
          [=](QString message) { showNotification("", message); });

  connect(m_settingsWidget, &SettingsWidget::linkedDeviceNameChanged,
          m_settingsWidget, [=]() {
            // Re-apply to every account's profile for future loads, and to each
            // live page (Qt does not propagate profile script changes to an
            // existing page). Each account keeps its own label.
            WebEngineProfileManager::instance().applyUserSettings();
            for (const Account &account : m_accounts) {
              if (pageOf(account))
                pageOf(account)->runJavaScript(
                    LinkedDeviceName::scriptSource(account.name.isEmpty() ||
                                                           account.id.isEmpty()
                                                       ? QString()
                                                       : account.name));
            }
          });

  m_settingsWidget->appLockSetChecked(SettingsManager::instance()
                                          .settings()
                                          .value("lockscreen", false)
                                          .toBool());
}

void MainWindow::showSettings(bool isAskedByCLI) {
  if (m_lockWidget && m_lockWidget->getIsLocked()) {
    show();
    // Present the unlock screen rather than only nagging: the bug report was
    // being told to unlock with no unlock window anywhere on screen.
    ensureLockVisible();
    if (isAskedByCLI)
      showNotification(QApplication::applicationDisplayName() + tr("| Error"),
                       tr("Unlock to access Settings."));
    return;
  }
  if (m_webEngine == nullptr) {
    QMessageBox::critical(
        this, QApplication::applicationDisplayName() + tr("| Error"),
        tr("Unable to initialize settings module.\nWebengine is not initialized."));
    return;
  }
  if (!m_settingsWidget->isVisible()) {
    updateSettingsUserAgentWidget();
    m_settingsWidget->refresh();
    QRect screenRect = screen()->geometry();
    if (!screenRect.contains(m_settingsWidget->pos()))
      m_settingsWidget->move(screenRect.center() -
                             m_settingsWidget->rect().center());
    m_settingsWidget->show();
  }
}

void MainWindow::updateSettingsUserAgentWidget() {
  m_settingsWidget->updateDefaultUAButton(
      m_webEngine->page()->profile()->httpUserAgent());
}

void MainWindow::askToReloadPage() {
  QMessageBox msgBox;
  msgBox.setWindowTitle(QApplication::applicationDisplayName() + tr(" | Action required"));
  msgBox.setInformativeText(tr("Page needs to be reloaded to continue."));
  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.exec();
  doAppReload();
}

// ── Notifications ─────────────────────────────────────────────────────────────

void MainWindow::showNotification(QString title, QString message) {
  if (SettingsManager::instance()
          .settings()
          .value("disableNotificationPopups", false)
          .toBool())
    return;

  if (title.isEmpty())
    title = QApplication::applicationDisplayName();

  if (SettingsManager::instance()
              .settings()
              .value("notificationCombo", 0)
              .toInt() == 0) {
    auto timeout = SettingsManager::instance()
                       .settings()
                       .value("notificationTimeOut", 9000)
                       .toInt();

#ifdef Q_OS_LINUX
    auto ntf = notify(title, message, timeout);
    QObject::connect(ntf.get(), &Notification::Event::actionInvoked, this,
                     [this] (const QString & action) {
                       qDebug() << "Action: " << action;
                       if (action == "open")
                         this->notificationClicked();
                     });

    // Ship a high-resolution icon in the image-data hint so the logo stays
    // crisp on large notification popups (e.g. Cinnamon on Linux Mint, where a
    // 32px icon rendered blurry — issue #2). Loaded straight from resources so
    // it never depends on the icon theme resolving a big enough size, and
    // image-data takes precedence over the named icon anyway.
    ntf->setHint("image-data",
                 notificationImageHint(QPixmap(":/icons/app/icon-256.png")));
    ntf->show();
    return;
#else
    // Native notifications via the system tray (toast notifications on
    // Windows 10+); falls back to the popup below when no tray is available.
    if (m_systemTrayIcon && QSystemTrayIcon::supportsMessages()) {
      // A new toast replaces the visible one, so route messageClicked to
      // the handler of the most recent notification only.
      disconnect(m_trayNotificationClickConnection);
      m_trayNotificationClickConnection =
          connect(m_systemTrayIcon, &QSystemTrayIcon::messageClicked, this,
                  &MainWindow::notificationClicked);
      m_systemTrayIcon->showMessage(title, message, windowIcon(), timeout);
      return;
    }
#endif
  }

  auto popup = new NotificationPopup(m_webEngine);
  connect(popup, &NotificationPopup::notification_clicked, this,
          [=]() { notificationClicked(); });
  popup->style()->polish(qApp);
  popup->setMinimumWidth(300);
  popup->adjustSize();
  QScreen *scr = this->screen();
  if (scr) {
    popup->present(scr, title, message,
                   QPixmap(":/icons/app/notification/whatly-notify.png"));
  } else {
    qWarning() << "showNotification: unable to get a screen";
  }
}

void MainWindow::notificationClicked() {
  QWidget *w = frontWindow();
  w->show();
  QCoreApplication::processEvents();
  bringForward(w);
  // Quick reply: put the caret in the message box so the user can just type.
  if (m_webEngine && m_webEngine->page())
    m_webEngine->page()->runJavaScript(QuickReply::focusComposerScript());
}

// ── Lifecycle events ──────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event) {
  saveWindowGeometry();
  getPageTheme();
  QTimer::singleShot(500, m_settingsWidget,
                     [=]() { m_settingsWidget->refresh(); });

  // Hiding to the tray is only safe while the tray icon is actually there to
  // bring the window back; with it hidden, honour the close as a real quit.
  if (!m_isQuitting && QSystemTrayIcon::isSystemTrayAvailable() &&
      m_systemTrayIcon && m_systemTrayIcon->isVisible() &&
      SettingsManager::instance()
              .settings()
              .value("closeButtonActionCombo", 0)
              .toInt() == 0) {
    lockOnHideIfEnabled();
    hide();
    event->ignore();
    return;
  }
  event->accept();
  quitApp();
  QMainWindow::closeEvent(event);
}

void MainWindow::quitApp() {
  // Flush the final arrangement while the windows still exist and BEFORE
  // m_isQuitting blocks further saves — otherwise the collapse loop and the
  // teardown of the emptied detached windows would overwrite the layout with a
  // single-window state. Also clear the crash guard for this clean shutdown.
  saveWindowLayout();
  SettingsManager::instance().settings().setValue(
      QStringLiteral("windowLayout/restoreInProgress"), false);
  m_isQuitting = true;
  // Reparent any torn-off account views back into the main stack, so every view
  // is owned by the main window again and torn down in a known order at exit.
  // Collect the (possibly shared) detached windows so each is hidden and
  // scheduled for deletion exactly once — otherwise the now-empty top-levels
  // linger on screen during the async getPageTheme() shutdown delay and leak.
  QSet<DetachedAccountWindow *> detachedWindows;
  for (int i = 0; i < m_accounts.size(); ++i)
    if (m_accounts[i].window) {
      if (m_accounts[i].view)
        m_accountStack->addWidget(m_accounts[i].view);
      detachedWindows.insert(m_accounts[i].window);
      m_accounts[i].window = nullptr;
    }
  for (DetachedAccountWindow *win : detachedWindows)
    if (win) {
      win->hide();
      win->deleteLater();
    }
  saveWindowGeometry();
  getPageTheme();
  // Give the async getPageTheme() call above time to land before quitting.
  QTimer::singleShot(500, this, [=]() { qApp->quit(); });
}

void MainWindow::runMinimized() { m_minimizeAction->trigger(); }

void MainWindow::alreadyRunning() {
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  show();
}

// ── Dialogs ───────────────────────────────────────────────────────────────────

void MainWindow::showAbout() {
  About *about = new About(this);
  about->setWindowFlag(Qt::Dialog);
  about->setMinimumSize(about->sizeHint());
  about->adjustSize();
  about->setAttribute(Qt::WA_DeleteOnClose, true);
  about->show();
}

void MainWindow::showScheduledMessages() {
  auto *dialog = new ScheduledMessagesDialog(m_scheduledMessages, this);
  dialog->setWindowFlag(Qt::Dialog);
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  dialog->show();
}

void MainWindow::commandSend(const Messaging::SendCommand &cmd) {
  using namespace Messaging;

  // App Lock gate (issue #41): while the app is locked, refuse to send. This is
  // the single choke point for the interactive Quick Compose box, the CLI
  // `--send` and the local HTTP API. Scheduled messages have their own delivery
  // path (not this one), so they still fire while locked, as intended.
  if (m_lockWidget && m_lockWidget->getIsLocked()) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("Whatly is locked. Unlock it to send messages."));
    return;
  }

  if (cmd.backend == Backend::Cloud) {
    // The Cloud API needs no page. The CLI sends cloud headlessly before this
    // IPC handler is reached, but the local HTTP API routes cloud sends here, so
    // perform them directly.
    const Recipient rc = parseRecipient(cmd.to);
    if (rc.kind != RecipientKind::PhoneNumber) {
      showNotification(QApplication::applicationDisplayName(),
                       tr("The Cloud API needs a phone number as the recipient."));
      return;
    }
    if (!CloudApi::isConfigured()) {
      showNotification(QApplication::applicationDisplayName(),
                       tr("The Cloud API is not configured."));
      return;
    }
    const CloudApi::Result res =
        cmd.file.isEmpty() ? CloudApi::sendText(rc.value, cmd.message)
                           : CloudApi::sendMediaFile(rc.value, cmd.file,
                                                     cmd.message);
    if (!res.ok)
      showNotification(QApplication::applicationDisplayName(),
                       tr("Cloud API send failed: %1").arg(res.error));
    return;
  }

  const Recipient r = parseRecipient(cmd.to);
  if (r.kind == RecipientKind::PhoneNumber) {
    if (!cmd.file.isEmpty()) {
      sendAttachmentViaWeb(r.value, cmd.file, cmd.message);
      return;
    }
    // Reuse the scheduled-message automation: an entry due now is sent
    // immediately (add() calls checkDue()), through WhatsApp Web, with the same
    // one-in-flight queue and result reporting.
    if (m_scheduledMessages)
      m_scheduledMessages->add(r.value, QString(), cmd.message,
                               QDateTime::currentDateTime());
    return;
  }

  if (r.kind == RecipientKind::ContactName || r.kind == RecipientKind::GroupId) {
    sendByNameViaWeb(r, cmd.message, cmd.file);
    return;
  }

  showNotification(QApplication::applicationDisplayName(),
                   tr("Could not understand the recipient: %1").arg(cmd.to));
}

void MainWindow::sendByNameViaWeb(const Messaging::Recipient &recipient,
                                  const QString &text, const QString &filePath) {
  using namespace Messaging;
  if (!m_webEngine || !m_webEngine->page()) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("No WhatsApp window is open"));
    return;
  }
  // Leave a job the injected name-sender script (see nameSenderScriptSource)
  // picks up: it opens the chat by exact-title search (or, for a group id, via
  // the internal store) and then sends. No navigation, so the currently open
  // session and login are reused.
  QJsonObject job;
  job["kind"] = recipient.kind == RecipientKind::GroupId
                    ? QStringLiteral("groupid")
                    : QStringLiteral("name");
  job["query"] = recipient.value;
  job["text"] = text; // the message, or the caption when a file is attached

  // Optional attachment: read it here and ride it into the page as base64 (the
  // same size cap and transport as sendAttachmentViaWeb). When present, `text`
  // becomes the media caption.
  if (!filePath.isEmpty()) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
      showNotification(QApplication::applicationDisplayName(),
                       tr("Could not read the file to send: %1").arg(filePath));
      return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    constexpr qint64 kMaxWebAttachmentBytes = 3 * 1024 * 1024;
    if (bytes.size() > kMaxWebAttachmentBytes) {
      showNotification(
          QApplication::applicationDisplayName(),
          tr("The file is too large to send over the web backend."));
      return;
    }
    QJsonObject attach;
    attach["name"] = QFileInfo(filePath).fileName();
    attach["mime"] = QMimeDatabase().mimeTypeForFile(filePath).name();
    attach["b64"] = QString::fromLatin1(bytes.toBase64());
    job["attach"] = attach;
  }

  const QString jobJson =
      QString::fromUtf8(QJsonDocument(job).toJson(QJsonDocument::Compact));

  const QString js =
      QStringLiteral("(function(){try{var job=%1;"
                     "job.deadline=Date.now()+45000;"
                     "sessionStorage.setItem('whatlyNameJob',"
                     "JSON.stringify(job));}"
                     "catch(e){console.error('whatly name: '+e);}})();")
          .arg(jobJson);
  m_webEngine->page()->runJavaScript(js);

  showNotification(
      QApplication::applicationDisplayName(),
      recipient.kind == RecipientKind::GroupId
          ? tr("Opening the group and sending…")
          : tr("Opening the chat with \"%1\" and sending…").arg(recipient.value));
}

void MainWindow::handleCloudIncoming(const QString &from, const QString &text) {
  // A Cloud API webhook delivered an incoming message. Evaluate the same
  // auto-reply rules used for the web session; if one matches, reply straight
  // back through the Cloud API (no page needed).
  const QString reply = AutoReply::replyFor(text);
  if (reply.isEmpty())
    return;
  const CloudApi::Result res = CloudApi::sendText(from, reply);
  if (!res.ok)
    qWarning().noquote() << "Cloud webhook auto-reply to" << from
                         << "failed:" << res.error;
}

void MainWindow::startLocalApi() {
  if (!m_localApi)
    return;
  if (!LocalApi::isConfigured()) {
    m_localApi->stop();
    return;
  }
  QString error;
  if (m_localApi->start(&error)) {
    qInfo().noquote() << "Local API listening on"
                      << LocalApi::bindAddress() + ':' +
                             QString::number(m_localApi->listeningPort());
  } else {
    qWarning().noquote() << "Local API could not start:" << error;
    showNotification(QApplication::applicationDisplayName(),
                     tr("The local API could not start: %1").arg(error));
  }
}

void MainWindow::sendAttachmentViaWeb(const QString &number,
                                      const QString &path,
                                      const QString &caption) {
  if (!m_webEngine || !m_webEngine->page()) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("No WhatsApp window is open"));
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("Could not read the file to send: %1").arg(path));
    return;
  }
  const QByteArray bytes = f.readAll();
  f.close();
  // Mirror the CLI cap: the bytes ride into the page (base64 in sessionStorage,
  // which survives the navigation that opens the chat), so keep it small.
  constexpr qint64 kMaxWebAttachmentBytes = 3 * 1024 * 1024;
  if (bytes.size() > kMaxWebAttachmentBytes) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("The file is too large to send over the web backend."));
    return;
  }

  QJsonObject job;
  job["number"] = number;
  job["name"] = QFileInfo(path).fileName();
  job["mime"] = QMimeDatabase().mimeTypeForFile(path).name();
  job["b64"] = QString::fromLatin1(bytes.toBase64());
  job["caption"] = caption;
  const QString jobJson =
      QString::fromUtf8(QJsonDocument(job).toJson(QJsonDocument::Compact));

  // Store the job (a JS object literal — JSON is valid JS), then open the chat
  // via the deep link. The reload keeps sessionStorage, so the attachment
  // sender script injected on the next load finds the job and completes it.
  // `number` is digits-only (parseRecipient normalises it), so a plain quoted
  // literal is safe.
  const QString js =
      QStringLiteral("(function(){try{var job=%1;"
                     "sessionStorage.setItem('whatlyAttachJob',"
                     "JSON.stringify(job));"
                     "window.location.href='https://web.whatsapp.com/send?phone='"
                     "+'%2';}catch(e){console.error('whatly attach: '+e);}})();")
          .arg(jobJson, number);
  m_webEngine->page()->runJavaScript(js);
}

QWidget *MainWindow::frontWindow() const {
  // m_focusOrder is most-recently-focused first, with a null entry standing for
  // this window. Both this window and every detached one record their own
  // activation, so the front entry is the last window the user touched.
  DetachedAccountWindow *front =
      m_focusOrder.isEmpty() ? nullptr : m_focusOrder.first();
  return front ? static_cast<QWidget *>(front)
               : static_cast<QWidget *>(const_cast<MainWindow *>(this));
}

QList<QWidget *> MainWindow::allWindows() const {
  QList<QWidget *> out;
  out << const_cast<MainWindow *>(this);
  for (const Account &a : m_accounts)
    if (a.window && !out.contains(a.window))
      out << a.window;
  return out;
}

QList<QWidget *> MainWindow::windowsByFocus() const {
  // m_focusOrder is most-recently-focused first, with a null entry standing for
  // this window (see frontWindow()). Translate that, then let the pure helper do
  // the ordering — it is the part with the rules in it (repeats, windows since
  // closed, windows never focused) and the part worth testing.
  QList<QWidget *> history;
  for (DetachedAccountWindow *win : m_focusOrder)
    history << (win ? static_cast<QWidget *>(win)
                    : static_cast<QWidget *>(const_cast<MainWindow *>(this)));
  return Utils::orderedByHistory(history, allWindows());
}

void MainWindow::hideAllWindows() {
  for (QWidget *w : allWindows())
    w->hide();
}

void MainWindow::restoreAllWindows() {
  // Least-recently-used first, so what comes back is stacked the way it was
  // left, and the window the user was actually in ends up on top.
  const QList<QWidget *> order = windowsByFocus();
  for (int i = order.size() - 1; i > 0; --i) {
    QWidget *w = order[i];
    w->setWindowState(w->windowState() & ~Qt::WindowMinimized);
    w->show();
    w->raise();
  }
  bringForward(order.isEmpty() ? this : order.first());
}

void MainWindow::bringForward(QWidget *w) {
  if (!w)
    return;
  w->setWindowState((w->windowState() & ~Qt::WindowMinimized) |
                    Qt::WindowActive);
  w->show();
  w->raise();
  w->activateWindow();
}

void MainWindow::raiseWindow() { bringForward(frontWindow()); }

// Come back as the SAME program, launched the SAME way. applicationFilePath()
// rather than argv[0] (which can be a bare name found on PATH), and the real
// argument list rather than a reconstruction of it, so --profile=work or a
// --send still means what it meant.
//
// The new process cannot simply be started and left to it: SingleApplication
// keys on the profile, so a second instance of the same account would hand its
// arguments to the one still running and exit — and then nothing would be left.
// It is handed the current process id instead and waits for it to go before
// claiming the key. Nothing is killed: this window closes through the ordinary
// quit path, which is also what writes the window layout out.
void MainWindow::restartApp() {
  QStringList args = QCoreApplication::arguments();
  if (!args.isEmpty())
    args.removeFirst(); // argv[0]
  args.removeIf([](const QString &a) {
    return a.startsWith(QLatin1String("--restart-wait="));
  });
  args << QStringLiteral("--restart-wait=%1")
              .arg(QCoreApplication::applicationPid());

  if (m_settingsWidget)
    m_settingsWidget->saveUiState();
  saveWindowLayout();
  SettingsManager::instance().settings().sync(); // the new process reads these

  const QString exePath = QCoreApplication::applicationFilePath();
  const auto failed = [this]() {
    // Nothing has been closed yet, so a failure here costs the user nothing
    // beyond the message.
    QMessageBox::warning(this, tr("Restart"),
                         tr("Whatly could not start a new instance, so it has "
                            "not closed this one. Please quit and reopen it."));
  };

#ifdef Q_OS_UNIX
  // Hand the new process the SAME stdout and stderr this one has.
  // QProcess::startDetached deliberately does not — the child ends up on the
  // controlling terminal — so a launch whose output was being piped into a log
  // file stopped being logged the instant anything called this. "Restart now" is
  // a button we put in Settings, and one press of it silently ended the log
  // people are asked to attach to bug reports. Losing the log at the exact moment
  // someone is reproducing a problem is the worst possible time to lose it.
  //
  // fork+exec keeps the descriptors, and that is the whole difference: the
  // --restart-wait handshake, the arguments and the ordering are unchanged.
  if (!QFileInfo(exePath).isExecutable()) {
    failed();
    return;
  }
  // Everything the child needs is built HERE, before the fork: between fork and
  // exec only async-signal-safe calls are allowed, and allocating is not one.
  QList<QByteArray> argStore;
  argStore << exePath.toLocal8Bit();
  for (const QString &a : args)
    argStore << a.toLocal8Bit();
  QVarLengthArray<char *, 16> argv;
  for (QByteArray &a : argStore)
    argv.append(a.data());
  argv.append(nullptr);

  // fork, setsid, fork again — the dance startDetached does, and the part of it
  // that a bare fork was missing. A plain child stays in the dying parent's
  // session and process group, and does not survive it here: the new process got
  // as far as printing its start-up line and was then taken down with the old
  // one. It has to leave that session (setsid) and be orphaned onto init (the
  // second fork) to outlive the instance that started it. stdout and stderr
  // survive both forks untouched, which is the whole point of doing this at all;
  // every other descriptor is closed just before exec, below.
  const pid_t child = ::fork();
  if (child < 0) {
    failed();
    return;
  }
  if (child == 0) {
    if (::setsid() < 0)
      ::_exit(127);
    const pid_t grandchild = ::fork();
    if (grandchild < 0)
      ::_exit(127);
    if (grandchild == 0) {
      // Keep 0, 1 and 2 — they are the log, and the reason for all of this —
      // and close everything above them. QProcess::startDetached used to do
      // that for us, and dropping it cost the remote-debugging port: the old
      // process's listening socket came through exec, so the new process could
      // not bind it ("bind() failed: Address already in use", in the log of the
      // session that found this) and the inherited socket sat there listening
      // with nobody left to accept on it. Any other descriptor the old process
      // held — profile locks among them — would travel the same way.
      bool closed = false;
#ifdef SYS_close_range
      closed = ::syscall(SYS_close_range, 3, ~0U, 0) == 0;
#endif
      if (!closed) {
        const long maxFd = ::sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < int(maxFd > 0 ? maxFd : 4096); ++fd)
          ::close(fd);
      }
      ::execv(argStore.first().constData(), argv.data());
      // Only reachable if exec failed. Nobody is left to tell by now — the
      // executable was checked before the first fork for exactly that reason.
      ::_exit(127);
    }
    ::_exit(0); // the middle process has done its job; init adopts the grandchild
  }
  // Reap the middle process, which exits immediately. Without this it lingers as
  // a zombie for as long as this instance takes to quit, and --restart-wait
  // watches for a pid to disappear.
  int status = 0;
  ::waitpid(child, &status, 0);
#else
  if (!QProcess::startDetached(exePath, args)) {
    failed();
    return;
  }
#endif
  quitApp();
}

void MainWindow::toggleTheme() {
  if (m_settingsWidget != nullptr)
    m_settingsWidget->toggleTheme();
}

// The button in WhatsApp's rail is a switch, but the setting has five values.
// Flipping it off remembers which one was on, so flipping it back on restores
// what the user actually chose rather than resetting them to a default.
void MainWindow::togglePrivacyBlur() {
  QSettings &settings = SettingsManager::instance().settings();
  const QString current = PrivacyBlur::currentLevelId();

  if (current == QLatin1String("off")) {
    QString previous = settings.value(QStringLiteral("privacyBlurLast"))
                           .toString();
    if (previous.isEmpty() || previous == QLatin1String("off"))
      previous = QStringLiteral("both");
    PrivacyBlur::setCurrentLevelId(previous);
  } else {
    settings.setValue(QStringLiteral("privacyBlurLast"), current);
    PrivacyBlur::setCurrentLevelId(QStringLiteral("off"));
  }

  // Every account, not just the current one: the setting is app-wide, and in
  // grid view they are all on screen at once. See toggleChatListStrip().
  WebEngineProfileManager::instance().applyUserSettings();
  for (const Account &account : m_accounts)
    if (pageOf(account))
      pageOf(account)->runJavaScript(PrivacyBlur::scriptSource());
  if (m_settingsWidget)
    m_settingsWidget->refresh();   // keep the combo box telling the truth
}

// The command palette shows an action by its text, so the entry has to say what
// pressing it will DO, not what the feature is called.
void MainWindow::refreshChatListStripAction() {
  if (m_chatListStripAction)
    m_chatListStripAction->setText(ChatListStrip::isCollapsed()
                                       ? tr("Expand the chat list")
                                       : tr("Collapse the chat list"));
}

// Collapse WhatsApp's chat list to a strip of avatars, or bring it back.
// Reached from the button in WhatsApp's own rail and from the command palette.
void MainWindow::toggleChatListStrip() {
  ChatListStrip::setCollapsed(!ChatListStrip::isCollapsed());
  // Every account, not just the current one. The state is app-wide, but the
  // button that flips it sits inside a page — and in grid view every account is
  // on screen at once, so reaching only m_webEngine left the click apparently
  // doing nothing in the tile it was made in while a different tile collapsed.
  // applyUserSettings() reinstalls in every account's PROFILE for future loads;
  // the pages already open have to be told separately, as Qt does not propagate
  // a profile's script changes to them.
  WebEngineProfileManager::instance().applyUserSettings();
  for (const Account &account : m_accounts)
    if (pageOf(account))
      pageOf(account)->runJavaScript(ChatListStrip::scriptSource());
  refreshChatListStripAction();
}

void MainWindow::focusChatSearch() {
  // The account in the window being typed into, not the app-wide "active" one:
  // with a detached window in front, those are different accounts, and searching
  // the wrong one is worse than doing nothing.
  const int idx = focusedAccountIndex();
  if (idx < 0)
    return;
  // Which search to open is the page's decision, not ours: with a conversation
  // open the answer is the search within it, and only with none open is it the
  // chat list's box — which the script also has to expand the list for when it is
  // collapsed. All three of those are questions only the DOM can answer.
  //
  // Safe to ask the view for its page here, unlike a loop over every account:
  // this account is the one on screen, so its page already exists.
  if (m_accounts[idx].view && m_accounts[idx].view->page())
    m_accounts[idx].view->page()->runJavaScript(ChatNav::focusSearchScript());
}

// ── Chat / URL helpers ────────────────────────────────────────────────────────

void MainWindow::loadSchemaUrl(const QString &arg) {
  // A group-invite link (issue #186): open its "Join group" preview. This must
  // come before the send handling below so an invite is never treated as a send.
  const QString invite = inviteCodeFromUrl(arg);
  if (!invite.isEmpty()) {
    openGroupInvite(invite);
    return;
  }
  if (arg.contains("send?") || arg.contains("send/?")) {
    QString newArg = arg;
    newArg = newArg.replace("?", "&");
    QUrlQuery query(newArg);
    triggerNewChat(query.queryItemValue("phone"),
                   query.queryItemValue("text"));
  }
}

// WhatsApp Web only opens a group invite when a link carrying its
// chat.whatsapp.com href is clicked inside the page; navigating there or hitting
// an /accept route does nothing. So synthesise such a link and click it, which
// pops up WhatsApp's own "Join group" preview (verified live for issue #186).
// The code is validated to [A-Za-z0-9._-] by inviteCodeFromUrl, so embedding it
// in the script is safe.
void MainWindow::openGroupInvite(const QString &code) {
  if (code.isEmpty() || !m_webEngine || !m_webEngine->page())
    return;
  const QString js = QStringLiteral(
      "(function(){"
      "  var a=document.createElement('a');"
      "  a.href='https://chat.whatsapp.com/%1';"
      "  a.rel='noopener';"
      "  a.style.display='none';"
      "  document.body.appendChild(a);"
      "  a.dispatchEvent(new MouseEvent('click',"
      "    {bubbles:true,cancelable:true,view:window}));"
      "  setTimeout(function(){a.remove();},0);"
      "})();")
      .arg(code);
  m_webEngine->page()->runJavaScript(js);
}

#ifdef Q_OS_LINUX
// Serialize a pixmap into the freedesktop.org "image-data" notification hint.
//
// libnotify-qt's setIconFromPixmap() cannot be used: it converts to
// QImage::Format_ARGB32 and dumps the raw buffer. ARGB32 packs a pixel as the
// 32-bit value 0xAARRGGBB, so on little-endian machines the bytes land in
// memory as B,G,R,A — while the spec wants R,G,B,A. The daemon therefore reads
// red as blue and vice versa, which is why avatars showed up with swapped
// colours. Format_RGBA8888 is byte-ordered (R,G,B,A on every architecture), so
// it matches the spec exactly.
QVariant MainWindow::notificationImageHint(const QPixmap &pixmap) {
  const QImage img = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);

  QDBusArgument arg;
  arg.beginStructure();
  arg << img.width() << img.height()
      << static_cast<qint32>(img.bytesPerLine()) // rowstride
      << true                                    // has alpha
      << 8                                       // bits per sample
      << 4                                       // channels (R,G,B,A)
      // Deep copy: the argument outlives this function, so it must not
      // reference the local QImage's buffer.
      << QByteArray(reinterpret_cast<const char *>(img.constBits()),
                    static_cast<int>(img.sizeInBytes()));
  arg.endStructure();
  return QVariant::fromValue(arg);
}

Notification::EventPtr MainWindow::notify(const QString& title, const QString& body, qint32 timeout) {
  // The icon must be named after the installed icon / desktop id
  // (net.shakaran.whatly), not "whatly": the notification daemon resolves it
  // from the icon theme, and "whatly" does not exist there — especially in a
  // Flatpak, where only net.shakaran.whatly is present — so KDE showed a broken
  // "unknown app" logo (issue #38).
  Notification::EventPtr ntf = m_notifier.createNotification(title, body, kAppId);

  ntf->setTimeout(timeout);
  ntf->setCategory("im.received");
  ntf->addAction("open", tr("Open"));
  ntf->setHint("action-icons", false);
  ntf->setHintString("image-path", kAppId);
  // Ties the notification to the app's .desktop entry, so KDE labels it with the
  // app name and icon (and groups it correctly).
  ntf->setHintString("desktop-entry", kAppId);
  // Ask the notification service to play a sound for the message (issue #120).
  // "message-new-instant" is the freedesktop sound-naming-spec event for a new
  // instant message; the daemon (KDE, GNOME, ...) maps it to its own sound. Some
  // desktops stay silent without this hint, so it is on by default.
  if (SettingsManager::instance()
          .settings()
          .value("notificationSound", true)
          .toBool())
    ntf->setHintString("sound-name", "message-new-instant");
  return ntf;
}
#endif

void MainWindow::showQuickCompose() {
  // Don't even open the quick-compose box while locked (issue #41): the send
  // would be refused anyway, and the box should not appear over the lock screen.
  if (m_lockWidget && m_lockWidget->getIsLocked()) {
    showNotification(QApplication::applicationDisplayName(),
                     tr("Whatly is locked. Unlock it to send messages."));
    return;
  }
  if (!m_quickCompose) {
    m_quickCompose = new QuickCompose(nullptr);
    connect(m_quickCompose, &QuickCompose::submitted, this,
            [this](const QString &recipient, const QString &message) {
              // Reuse the web-send path: it parses a number vs a name and injects
              // into the running session, so it works with the window hidden.
              Messaging::SendCommand cmd;
              cmd.backend = Messaging::Backend::Web;
              cmd.to = recipient;
              cmd.message = message;
              commandSend(cmd);
            });
  }
  m_quickCompose->popUp();
}

void MainWindow::newChat() {
  bool ok;
  QString phoneNumber = QInputDialog::getText(
      this, tr("New Chat"),
      tr("Enter a valid WhatsApp number with country code (ex- +91XXXXXXXXXX)"),
      QLineEdit::Normal, "", &ok);
  if (ok)
    triggerNewChat(phoneNumber, "");
}

void MainWindow::triggerNewChat(const QString &phone, const QString &text) {
  static QString phoneStr, textStr;
  m_webEngine->page()->runJavaScript(
      "openNewChatWhatlyDefined()",
      [this, phone, text](const QVariant &result) {
        if (result.toString().contains("true")) {
          m_webEngine->page()->runJavaScript(
              QString("openNewChatWhatly(\"%1\",\"%2\")").arg(phone, text));
        } else {
          phoneStr = phone.isEmpty() ? "" : "phone=" + phone;
          textStr = text.isEmpty() ? "" : "text=" + text;
          m_webEngine->page()->load(
              QUrl("https://web.whatsapp.com/send?" + phoneStr + "&" +
                   textStr));
        }
        alreadyRunning();
      });
}

// ── Rate widget ───────────────────────────────────────────────────────────────

void MainWindow::initRateWidget() {
  RateApp *rateApp = new RateApp(this, "snap://whatly", 5, 5, 1000 * 30);
  rateApp->setWindowTitle(QApplication::applicationDisplayName() + " | " +
                          tr("Rate Application"));
  rateApp->setVisible(false);
  rateApp->setWindowFlags(Qt::Dialog);
  rateApp->setAttribute(Qt::WA_DeleteOnClose, true);
  QPoint centerPos = geometry().center() - rateApp->geometry().center();
  connect(rateApp, &RateApp::showRateDialog, rateApp, [=]() {
    if (windowState() != Qt::WindowMinimized && isVisible() &&
        isActiveWindow()) {
      rateApp->move(centerPos);
      rateApp->show();
    } else {
      rateApp->delayShowEvent();
    }
  });
}
