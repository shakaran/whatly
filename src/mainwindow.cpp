// Core MainWindow: constructor, lifecycle events, settings UI, notifications,
// zoom, and navigation helpers.
// WebEngine, tray, and lock logic live in mainwindow_webengine/tray/lock.cpp.
#include "mainwindow.h"
#include "appprofile.h"

#include <algorithm>
#include <QInputDialog>
#include <QRegularExpression>
#include <QScreen>
#include <QStyleFactory>
#include <QUrlQuery>
#ifdef Q_OS_LINUX
#include <QDBusArgument>
#endif

#include "about.h"
#include "common.h"
#include "linkeddevicename.h"
#include "rateapp.h"
#include "theme.h"
#include "chattheme.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "privacyblur.h"
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
      m_notifier("WhatSie", this),
#endif
      m_trayIconNormal(themeIcon("whatsie-tray", ":/icons/app/notification/whatsie-notify.png")),
      m_notificationsTitleRegExp("^\\([1-9]\\d*\\).*"),
      m_unreadMessageCountRegExp("\\([^\\d]*(\\d+)[^\\d]*\\)") {

  setObjectName("MainWindow");
  setWindowTitle(QApplication::applicationName() + AppProfile::label());
  setWindowIcon(themeIcon("whatsie", ":/icons/app/icon-64.png"));
  applyMinimumSize();
  restoreMainWindow();
  createActions();
  createTrayIcon();
  createWebEngine();
  initSettingWidget();
  initRateWidget();
  QApplication::processEvents();
  tryLock();
  updateWindowTheme();
  initAutoLock();
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
  setMinimumWidth(static_cast<int>(kBaseMinWidth * factor));
  setMinimumHeight(static_cast<int>(kBaseMinHeight * factor));
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

// ── Theme ─────────────────────────────────────────────────────────────────────

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
  m_settingsWidget->setWindowTitle(QApplication::applicationName() +
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

  connect(m_settingsWidget, &SettingsWidget::customCssChanged, m_settingsWidget,
          [=]() {
            CustomCss::install(WebEngineProfileManager::instance().profile());
            for (const Account &account : m_accounts)
              if (account.view && account.view->page())
                account.view->page()->runJavaScript(CustomCss::scriptSource());
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
                                     "'whatsie-chat-theme'); if (e) e.remove();"
                                     "window.__whatsieChatThemeApply = null;})();")
                    : js);
          });

  connect(m_settingsWidget, &SettingsWidget::privacyBlurChanged,
          m_settingsWidget, [=]() {
            PrivacyBlur::install(WebEngineProfileManager::instance().profile());
            if (m_webEngine && m_webEngine->page())
              m_webEngine->page()->runJavaScript(PrivacyBlur::scriptSource());
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
      showNotification(QApplication::applicationName() + "| Error",
                       tr("Unlock to access Settings."));
    return;
  }
  if (m_webEngine == nullptr) {
    QMessageBox::critical(
        this, QApplication::applicationName() + "| Error",
        "Unable to initialize settings module.\nWebengine is not initialized.");
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
  msgBox.setWindowTitle(QApplication::applicationName() + " | Action required");
  msgBox.setInformativeText("Page needs to be reloaded to continue.");
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
    title = QApplication::applicationName();

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

    ntf->setHint("image-data",
                 notificationImageHint(windowIcon().pixmap(
                     windowIcon().actualSize(QSize(32, 32), QIcon::Normal),
                     QIcon::Normal, QIcon::On)));
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
                   QPixmap(":/icons/app/notification/whatsie-notify.png"));
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

  if (!m_isQuitting && QSystemTrayIcon::isSystemTrayAvailable() &&
      SettingsManager::instance()
              .settings()
              .value("closeButtonActionCombo", 0)
              .toInt() == 0) {
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
  Notification::EventPtr ntf = m_notifier.createNotification(title, body, "whatsie");

  ntf->setTimeout(timeout);
  ntf->setCategory("im.received");
  ntf->addAction("open", tr("Open"));
  ntf->setHint("action-icons", false);
  ntf->setHintString("image-path", "whatsie");
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
      "openNewChatWhatsieDefined()",
      [this, phone, text](const QVariant &result) {
        if (result.toString().contains("true")) {
          m_webEngine->page()->runJavaScript(
              QString("openNewChatWhatsie(\"%1\",\"%2\")").arg(phone, text));
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
  RateApp *rateApp = new RateApp(this, "snap://whatsie", 5, 5, 1000 * 30);
  rateApp->setWindowTitle(QApplication::applicationName() + " | " +
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
