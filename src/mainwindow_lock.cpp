// App lock, auto-lock, and session-logout helpers.
#include "mainwindow.h"
#include "screenlock.h"

#include "common.h"

// ── Lock initialisation ───────────────────────────────────────────────────────

void MainWindow::initAutoLock() {
  m_autoLockEventFilter = new AutoLockEventFilter(
      SettingsManager::instance()
          .settings()
          .value("autoLockDuration", defaultAppAutoLockDuration)
          .toInt() *
      1000);
  connect(m_autoLockEventFilter, &AutoLockEventFilter::autoLockTimerTimeout,
          this, [=]() {
            if ((m_settingsWidget && !m_settingsWidget->isVisible()) &&
                SettingsManager::instance()
                    .settings()
                    .value("appAutoLocking", defaultAppAutoLock)
                    .toBool()) {
              this->lockApp();
            }
          });
  if (SettingsManager::instance()
          .settings()
          .value("appAutoLocking", defaultAppAutoLock)
          .toBool()) {
    qApp->installEventFilter(m_autoLockEventFilter);
  }
}

void MainWindow::initLock() {
  if (m_lockWidget == nullptr) {
    // Parented to the central widget, not to the window. QMainWindow's layout
    // keeps the central widget stacked above any plain child of the window, so
    // a lock overlay parented to `this` ends up *behind* the account area and
    // never covers it — you are told to unlock with no unlock screen in sight.
    // Inside the central widget it is a normal sibling that raise() keeps on
    // top. (This is the account-tabs container; before that it was the view.)
    QWidget *host = centralWidget() ? centralWidget() : qobject_cast<QWidget *>(this);
    m_lockWidget = new Lock(host);
    m_lockWidget->setObjectName("lockWidget");
    m_lockWidget->setWindowFlags(Qt::Widget);
    m_lockWidget->setStyleSheet(
        "QWidget#login{background-color:palette(window)};"
        "QWidget#signup{background-color:palette(window)}");
    m_lockWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(m_lockWidget, &Lock::passwordNotSet, m_settingsWidget, [=]() {
      SettingsManager::instance().settings().setValue("lockscreen", false);
      m_settingsWidget->appLockSetChecked(false);
    });

    connect(m_lockWidget, &Lock::unLocked, [=]() {
      // unlock event
    });

    connect(m_lockWidget, &Lock::passwordSet, m_settingsWidget, [=]() {
      if (SettingsManager::instance().settings().value("asdfg").isValid()) {
        // Passcode is stored hashed (issue #42): show a mask, not the value.
        m_settingsWidget->setCurrentPasswordText(QString());
      } else {
        m_settingsWidget->setCurrentPasswordText("Require setup");
      }
      m_settingsWidget->appLockSetChecked(SettingsManager::instance()
                                              .settings()
                                              .value("lockscreen", false)
                                              .toBool());
    });
    m_lockWidget->applyThemeQuirks();
  }

  if (centralWidget()) m_lockWidget->setGeometry(centralWidget()->rect());
  QApplication::processEvents();

  if (SettingsManager::instance().settings().value("lockscreen").toBool()) {
    if (SettingsManager::instance().settings().value("asdfg").isValid()) {
      m_lockWidget->lock_app();
    } else {
      m_lockWidget->signUp();
    }
    m_lockWidget->show();
    // Above the central widget, always. The central widget is now a container
    // (the account tab area), and a lock overlay left behind it is locked with
    // no way to unlock — exactly the "told to unlock, no unlock window" report.
    m_lockWidget->raise();
  } else {
    m_lockWidget->hide();
  }
  updateWindowTheme();
}

// The lock is on but its overlay is not actually in front of the user — bring
// it there. Called wherever the app refuses an action because it is locked, so
// "unlock first" always comes with something to unlock.
void MainWindow::ensureLockVisible() {
  if (!m_lockWidget || !m_lockWidget->getIsLocked())
    return;
  if (centralWidget())
    m_lockWidget->setGeometry(centralWidget()->rect());
  m_lockWidget->show();
  m_lockWidget->raise();
}

void MainWindow::tryLock() {
  if (SettingsManager::instance().settings().value("asdfg").isValid()) {
    initLock();
    return;
  }
  // No password configured — reset lock-related settings.
  SettingsManager::instance().settings().setValue("lockscreen", false);
  SettingsManager::instance().settings().setValue("appAutoLocking", false);
  m_settingsWidget->appAutoLockingSetChecked(false);
  m_settingsWidget->appLockSetChecked(false);
  initLock();
}

// ── Lock/unlock actions ───────────────────────────────────────────────────────

void MainWindow::lockApp() {
  if (m_lockWidget != nullptr && m_lockWidget->getIsLocked())
    return;

  if (SettingsManager::instance().settings().value("asdfg").isValid()) {
    initLock();
    m_lockWidget->lock_app();
  } else {
    int ret = QMessageBox::information(
        this, tr(QApplication::applicationDisplayName().toUtf8()),
        tr("App lock is not configured, \n"
           "Please setup the password in the Settings first.\n\nOpen "
           "Settings now?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ret == QMessageBox::Yes)
      this->showSettings();
  }
}

// Lock silently when the window hides to the tray, if the user asked for it and
// a passcode is actually configured. Unlike lockApp() this never nags about a
// missing password — hiding to the tray should not pop a dialog.
void MainWindow::lockOnHideIfEnabled() {
  if (!SettingsManager::instance()
           .settings()
           .value("lockOnHideToTray", false)
           .toBool())
    return;
  if (!SettingsManager::instance().settings().value("asdfg").isValid())
    return;
  if (m_lockWidget != nullptr && m_lockWidget->getIsLocked())
    return;
  initLock();
  m_lockWidget->lock_app();
}

#ifdef Q_OS_LINUX
// The session's screensaver/lock became active (or inactive). When it activated
// and the user opted in with a passcode configured, lock Whatly too.
void MainWindow::onScreenSaverActiveChanged(bool active) {
  const bool passcodeConfigured =
      SettingsManager::instance().settings().value("asdfg").isValid();
  if (!ScreenLock::shouldLock(active, passcodeConfigured))
    return;
  if (m_lockWidget != nullptr && m_lockWidget->getIsLocked())
    return;
  initLock();
  m_lockWidget->lock_app();
}
#endif

void MainWindow::changeLockPassword() {
  SettingsManager::instance().settings().remove("asdfg");
  m_settingsWidget->appLockSetChecked(false);
  m_settingsWidget->autoAppLockSetChecked(false);
  m_settingsWidget->updateAppLockPasswordViewer();
  tryLogOut();
  QTimer::singleShot(1000, this, [=]() {
    if (isLoggedIn()) {
      forceLogOut();
      doAppReload();
    }
    appAutoLockChanged();
    initLock();
  });
}

void MainWindow::appAutoLockChanged() {
  bool enabled = SettingsManager::instance()
                     .settings()
                     .value("appAutoLocking", defaultAppAutoLock)
                     .toBool();
  if (enabled) {
    m_autoLockEventFilter->setTimeoutmillis(
        SettingsManager::instance()
            .settings()
            .value("autoLockDuration", defaultAppAutoLockDuration)
            .toInt() *
        1000);
    qApp->installEventFilter(m_autoLockEventFilter);
    m_autoLockEventFilter->resetTimer();
  } else {
    m_autoLockEventFilter->stopTimer();
    qApp->removeEventFilter(m_autoLockEventFilter);
  }
}

// ── Session helpers ───────────────────────────────────────────────────────────

void MainWindow::forceLogOut() {
  if (m_webEngine && m_webEngine->page()) {
    m_webEngine->page()->runJavaScript(
        "window.localStorage.clear();",
        [=](const QVariant &result) { qDebug() << result; });
  }
}

bool MainWindow::isLoggedIn() {
  static bool loggedIn = false;
  if (m_webEngine && m_webEngine->page()) {
    m_webEngine->page()->runJavaScript(
        "window.localStorage.getItem('last-wid-md')",
        [=](const QVariant &result) {
          qDebug() << Q_FUNC_INFO << result;
          if (result.isValid() && !result.toString().isEmpty())
            loggedIn = true;
        });
    qDebug() << "isLoggedIn" << loggedIn;
    return loggedIn;
  }
  qDebug() << "isLoggedIn" << loggedIn;
  return loggedIn;
}

void MainWindow::tryLogOut() {
  if (m_webEngine && m_webEngine->page()) {
    m_webEngine->page()->runJavaScript(
        "document.querySelector(\"span[data-testid|='menu']\").click();"
        "document.querySelector(\"#side > header > div > div > span > div > "
        "span > div > ul > li:nth-child(4) > div\").click();"
        "var dialogEle,dialogEleLastElem;"
        "function logoutC(){"
        "  dialogEle=document.activeElement.querySelectorAll(\":last-child\");"
        "  dialogEleLastElem=dialogEle[dialogEle.length-1];"
        "  dialogEleLastElem.click();"
        "}"
        "setTimeout(logoutC, 600);",
        [=](const QVariant &result) { qDebug() << Q_FUNC_INFO << result; });
  }
}
