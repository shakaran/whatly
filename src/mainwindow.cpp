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
#include <QStyleFactory>
#include <QStyleHints>
#include <QUrlQuery>
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
#include "linkeddevicename.h"
#include "rateapp.h"
#include "theme.h"
#include "chattheme.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "customjs.h"
#include "setupwizard.h"
#include "customtitlebar.h"

#include <QTimer>
#include "privacyblur.h"
#include "webfont.h"
#include "mutedstatus.h"
#include "scheduledmessages.h"
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
      m_notifier("Whatly", this),
#endif
      m_trayIconNormal(themeIcon("whatly-tray", ":/icons/app/notification/whatly-notify.png")),
      m_notificationsTitleRegExp("^\\([1-9]\\d*\\).*"),
      m_unreadMessageCountRegExp("\\([^\\d]*(\\d+)[^\\d]*\\)") {

  setObjectName("MainWindow");
  setWindowTitle(QApplication::applicationDisplayName() + AppProfile::label());
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

  createWebEngine();
  initSettingWidget();
  initRateWidget();
  QApplication::processEvents();
  tryLock();
  updateWindowTheme();
  initAutoLock();

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
    connect(m_globalShortcut, &GlobalShortcut::activated, this, [this]() {
      setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
      show();
      raise();
      activateWindow();
    });
  } else {
    qInfo() << "No global-shortcut backend available; bind a desktop shortcut "
               "to `whatly -w` to raise the window.";
  }
#endif

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
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  trackNormalGeometry();
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
  if (e->type() == QEvent::WindowStateChange)
    handleZoomOnWindowStateChange(static_cast<QWindowStateChangeEvent *>(e));
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
  const double next =
      std::clamp(s.value(key, def).toDouble() + delta, 0.3, 3.0);
  s.setValue(key, next);
  m_webEngine->page()->setZoomFactor(next);
  applyMinimumSize();
}

void MainWindow::zoomIn() { zoomBy(0.1); }
void MainWindow::zoomOut() { zoomBy(-0.1); }

void MainWindow::zoomReset() {
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
  if (dark) {
    qApp->setPalette(Theme::getDarkPalette());
    m_webEngine->setStyleSheet("QWebEngineView{background:rgb(17, 27, 33);}");
  } else {
    qApp->setPalette(Theme::getLightPalette());
    m_webEngine->setStyleSheet("QWebEngineView{background:#F0F0F0;}");
  }

  if (m_webEngine->page()) {
    m_webEngine->page()->setBackgroundColor(
        dark ? QColor(17, 27, 33) : QColor(240, 240, 240));
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

  connect(m_settingsWidget, &SettingsWidget::webTweaksChanged, m_settingsWidget,
          [=]() {
            // Update the profile scripts for future page loads, and apply the
            // change to the already-loaded page (Qt does not propagate profile
            // script changes to an existing page).
            WebTweaks::install(WebEngineProfileManager::instance().profile());
            if (m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(WebTweaks::scriptSource());
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
              if (account.view && account.view->page())
                account.view->page()->runJavaScript(CustomCss::scriptSource());
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
            PrivacyBlur::install(WebEngineProfileManager::instance().profile());
            if (m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(PrivacyBlur::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::fontChanged, m_settingsWidget,
          [=]() {
            WebFont::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (account.view && account.view->page())
                account.view->page()->runJavaScript(WebFont::scriptSource());
          });

  connect(m_settingsWidget, &SettingsWidget::mutedStatusChanged,
          m_settingsWidget, [=]() {
            MutedStatus::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (account.view && account.view->page())
                account.view->page()->runJavaScript(MutedStatus::scriptSource());
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
              if (account.view && account.view->page())
                account.view->page()->runJavaScript(
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
  show();
  QCoreApplication::processEvents();
  if (windowState().testFlag(Qt::WindowMinimized))
    setWindowState(windowState() & ~Qt::WindowMinimized);
  raise();
  activateWindow();
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
  m_isQuitting = true;
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

  PrivacyBlur::install(WebEngineProfileManager::instance().profile());
  if (m_webEngine && m_webEngine->page())
    m_webEngine->page()->runJavaScript(PrivacyBlur::scriptSource());
  if (m_settingsWidget)
    m_settingsWidget->refresh();   // keep the combo box telling the truth
}

// ── Chat / URL helpers ────────────────────────────────────────────────────────

void MainWindow::loadSchemaUrl(const QString &arg) {
  if (arg.contains("send?") || arg.contains("send/?")) {
    QString newArg = arg;
    newArg = newArg.replace("?", "&");
    QUrlQuery query(newArg);
    triggerNewChat(query.queryItemValue("phone"),
                   query.queryItemValue("text"));
  }
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
  Notification::EventPtr ntf = m_notifier.createNotification(title, body, "whatly");

  ntf->setTimeout(timeout);
  ntf->setCategory("im.received");
  ntf->addAction("open", tr("Open"));
  ntf->setHint("action-icons", false);
  ntf->setHintString("image-path", "whatly");
  return ntf;
}
#endif

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
