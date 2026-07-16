// WebEngine page/profile lifecycle, reload, download, and page-theme handling.
#include "mainwindow.h"

#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QFile>
#include <QRandomGenerator>
#include <QScreen>

#include "common.h"
#include "webenginenotifproxy.h"
#include "webengineprofilemanager.h"
#include "webview.h"
#include "identicons.h"

// ── WebEngine view & page ─────────────────────────────────────────────────────
void MainWindow::createWebEngine() {
  WebEngineProfileManager::instance().applyUserSettings();

  // The central widget is now a tab bar over a stack of account views, rather
  // than a single view. With only the default account the tab bar hides itself,
  // so this is invisible until a second account is added.
  buildAccountArea();
  loadAccounts();

  // Connection watchdog: poll the injected WebSocket health probe and reload
  // the page when WhatsApp's socket has died or gone silent.
  if (!m_connectionWatchdog) {
    m_connectionWatchdog = new QTimer(this);
    m_connectionWatchdog->setInterval(20000);
    connect(m_connectionWatchdog, &QTimer::timeout, this,
            &MainWindow::checkConnectionHealth);
    m_connectionWatchdog->start();
  }
}

// The single-account entry point, kept for the reload paths. It reloads the
// active account.
void MainWindow::createWebPage(bool offTheRecord) {
  Q_UNUSED(offTheRecord);
  if (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
    createPageFor(m_accounts[m_activeAccount].view,
                  m_accounts[m_activeAccount].id);
}

void MainWindow::createPageFor(WebView *view, const QString &accountId) {
  QWebEngineProfile *profile =
      WebEngineProfileManager::instance().profileFor(accountId);
  WebEngineProfileManager::instance().applyUserSettings();

  setNotificationPresenter(profile);

  QWebEnginePage *page = new WebEnginePage(profile, view);
  installPageBridge(page);
  if (SettingsManager::instance()
          .settings()
          .value("windowTheme", "light")
          .toString() == "dark") {
    page->setBackgroundColor(QColor(17, 27, 33));   // WhatsApp dark bg
  } else {
    page->setBackgroundColor(QColor(240, 240, 240)); // WhatsApp light bg
  }
  view->setPage(page);

  auto randomValue = QRandomGenerator::global()->generateDouble() * 300.0;
  page->setUrl(
      QUrl("https://web.whatsapp.com?v=" + QString::number(randomValue)));

  connect(profile, &QWebEngineProfile::downloadRequested,
          &m_downloadManagerWidget, &DownloadManagerWidget::downloadRequested);
  connect(page, &QWebEnginePage::fullScreenRequested, this,
          &MainWindow::fullScreenRequested);

  double currentFactor = SettingsManager::instance()
                             .settings()
                             .value("zoomFactor", 1.0)
                             .toDouble();
  view->page()->setZoomFactor(currentFactor);
}

// Buttons WhatSie injects into WhatsApp's own UI need a way back into the app.
// QWebChannel is that way: it exposes exactly one object, with exactly the slots
// PageBridge declares — the page can reach nothing else.
void MainWindow::installPageBridge(QWebEnginePage *page) {
  if (!m_pageBridge) {
    m_pageBridge = new PageBridge(this);
    connect(m_pageBridge, &PageBridge::themeToggleRequested, this,
            &MainWindow::toggleTheme);
    connect(m_pageBridge, &PageBridge::privacyBlurToggleRequested, this,
            &MainWindow::togglePrivacyBlur);
  }
  if (!m_webChannel) {
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject(QStringLiteral("whatsieBridge"), m_pageBridge);
  }
  page->setWebChannel(m_webChannel);

  // Qt puts the transport in place, but the page still has to speak the
  // protocol: qwebchannel.js ships inside the QtWebChannel library as a
  // resource, and is injected here rather than vendored into the tree.
  QFile js(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
  if (!js.open(QIODevice::ReadOnly)) {
    qWarning() << "qwebchannel.js is missing; injected page buttons will do "
                  "nothing";
    return;
  }

  QWebEngineScript bridge;
  bridge.setName(QStringLiteral("whatsie-page-bridge"));
  bridge.setSourceCode(QString::fromUtf8(js.readAll()) + QStringLiteral(R"js(
    (function connect() {
      if (typeof qt === 'undefined' || !qt.webChannelTransport) {
        setTimeout(connect, 50);   // the transport lands slightly after us
        return;
      }
      new QWebChannel(qt.webChannelTransport, function (channel) {
        window.__whatsieBridge = channel.objects.whatsieBridge;
      });
    })();
  )js"));
  bridge.setInjectionPoint(QWebEngineScript::DocumentCreation);
  bridge.setWorldId(QWebEngineScript::MainWorld);
  bridge.setRunsOnSubFrames(false);

  QWebEngineScriptCollection &scripts = page->scripts();
  const auto stale = scripts.find(bridge.name());
  for (const auto &script : stale)
    scripts.remove(script);
  scripts.insert(bridge);
}

void MainWindow::setNotificationPresenter(QWebEngineProfile *profile) {
  if (m_webengine_notifier_popup != nullptr) {
    m_webengine_notifier_popup->close();
    m_webengine_notifier_popup->deleteLater();
  }

  m_webengine_notifier_popup = new NotificationPopup(m_webEngine);
  connect(m_webengine_notifier_popup, &NotificationPopup::notification_clicked,
          this, [this]() { notificationClicked(); });

  profile->setNotificationPresenter(
      [&](std::unique_ptr<QWebEngineNotification> notification) {
        QSettings &settings = SettingsManager::instance().settings();
        if (settings.value("disableNotificationPopups", false).toBool())
          return;

        int notificationCombo = settings.value("notificationCombo", 0).toInt();
        int timeout = settings.value("notificationTimeOut", 9000).toInt();

        if (notificationCombo == 0) {
#ifdef Q_OS_LINUX
          // Use Proxy to manage lifecycle of QWebEngineNotification safely
          auto proxy = WebEngineNotifProxy::create(std::move(notification));
          auto ntf = notify(proxy->notif->title(), proxy->notif->message(), timeout);
          // Use locally generated identicon when
          // QWebEngine (or whatsapp) passes blank
          // image
          QPixmap pix = [proxy](auto img) {
            return Identicons::colorCount(img) > 2
                ? QPixmap::fromImage(img)
                : Identicons::letterTile(proxy->notif->title(), QSize(96, 96));
          } (proxy->notif->icon());
          ntf->setHint("image-data", notificationImageHint(
                                        Identicons::clipRRect(pix) /* for eyecandy */));
          connect(ntf.get(), &Notification::Event::actionInvoked, this,
              [this, proxy](const QString & action) {
                if (action != "open") return;
                proxy->invoke(&QWebEngineNotification::click);
                this->notificationClicked();
              });

          connect(ntf.get(), &Notification::Event::closed, this,
              [this, proxy](Notification::ClosingReason reason) {
                proxy->invoke(&QWebEngineNotification::close);
              });

          ntf->show();
          proxy->invoke(&QWebEngineNotification::show);
          return;
#else
          // Native notifications via the system tray (toast notifications on
          // Windows 10+); falls back to the popup below when no tray is
          // available.
          if (m_systemTrayIcon && QSystemTrayIcon::supportsMessages()) {
            // Use Proxy to manage lifecycle of QWebEngineNotification safely
            auto proxy = WebEngineNotifProxy::create(std::move(notification));
            // Use locally generated identicon when
            // QWebEngine (or whatsapp) passes blank
            // image
            QPixmap pix = [proxy](auto img) {
              return Identicons::colorCount(img) > 2
                  ? QPixmap::fromImage(img)
                  : Identicons::letterTile(proxy->notif->title(), QSize(96, 96));
            } (proxy->notif->icon());
            // A new toast replaces the visible one, so route messageClicked
            // to the handler of the most recent notification only.
            disconnect(m_trayNotificationClickConnection);
            m_trayNotificationClickConnection = connect(
                m_systemTrayIcon, &QSystemTrayIcon::messageClicked, this,
                [this, proxy]() {
                  proxy->invoke(&QWebEngineNotification::click);
                  this->notificationClicked();
                });
            m_systemTrayIcon->showMessage(
                proxy->notif->title(), proxy->notif->message(),
                QIcon(Identicons::clipRRect(pix) /* for eyecandy */), timeout);
            proxy->invoke(&QWebEngineNotification::show);
            return;
          }
#endif
        }

        if (!m_webengine_notifier_popup) {
          qWarning() << "Popup is not available!";
          return;
        }

        m_webengine_notifier_popup->setMinimumWidth(300);
        // The screen this window lives on, not the primary one. On a
        // multi-monitor desk the popup used to appear on whichever screen the
        // system called primary, which is often not the one the user is
        // looking at. QWidget::screen() already falls back to the primary
        // screen when the window has no handle yet.
        QScreen *screen = this->screen();
        if (!screen) {
          const auto screens = QGuiApplication::screens();
          if (!screens.isEmpty()) {
            screen = screens.first();
          } else {
            qWarning() << "showNotification: unable to get any screen";
            return;
          }
        }
        m_webengine_notifier_popup->present(screen, notification);
      });
}

// ── Reload & load events ──────────────────────────────────────────────────────

void MainWindow::doAppReload() {
  if (m_webEngine->page())
    m_webEngine->page()->disconnect();
  createWebPage(false);
}

void MainWindow::doReload(bool byPassCache, bool isAskedByCLI,
                          bool byLoadingQuirk) {
  if (byLoadingQuirk) {
    m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache,
                                   byPassCache);
    return;
  }

  if (m_lockWidget && !m_lockWidget->getIsLocked()) {
    this->showNotification(QApplication::applicationName(),
                           QObject::tr("Reloading..."));
  } else {
    ensureLockVisible();   // give the user the unlock screen, not just a refusal
    QString error = tr("Unlock to Reload the App.");
    if (isAskedByCLI) {
      this->showNotification(QApplication::applicationName() + tr("| Error"),
                             error);
    } else {
      QMessageBox::critical(this, QApplication::applicationName() + tr("| Error"),
                            error);
    }
    this->show();
    return;
  }
  m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache,
                                 byPassCache);
}

void MainWindow::checkConnectionHealth() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  // Don't reload while the app is locked (would fight the lock screen).
  if (m_lockWidget && m_lockWidget->getIsLocked())
    return;
  // Cooldown: never auto-reload more than once per 60s, to avoid reload storms.
  if (m_lastWatchdogReload.isValid() && m_lastWatchdogReload.elapsed() < 60000)
    return;

  m_webEngine->page()->runJavaScript(
      QStringLiteral("(typeof window.__whatsieWsState==='function')?"
                     "window.__whatsieWsState():'idle'"),
      [this](const QVariant &result) {
        const QString state = result.toString();

        // Reflect the connection in the tray icon (see getTrayIcon). "ok" is
        // connected; "stuck" and "idle" (still connecting / offline) are not.
        const bool connected = state == QLatin1String("ok");
        if (connected != m_trayConnected) {
          m_trayConnected = connected;
          updateTrayUnread();   // repaint the tray icon in the new state
        }

        if (state == QLatin1String("ok")) {
          // Connection healthy again: reset so a future, unrelated hang gets a
          // fresh set of recovery attempts.
          m_watchdogStrikes = 0;
          m_watchdogReloads = 0;
          m_watchdogGaveUp = false;
          return;
        }

        if (state != QLatin1String("stuck")) {
          // "idle": still connecting or offline — nothing a reload would fix.
          m_watchdogStrikes = 0;
          return;
        }

        // Require two consecutive "stuck" reports (~20-40s) before acting, so a
        // brief reconnect gap or a momentarily idle socket is not mistaken for
        // a hang.
        if (++m_watchdogStrikes < 2)
          return;
        m_watchdogStrikes = 0;

        // Cap recovery at 3 reloads per hang episode. If the connection is still
        // stuck after 3 reloads the cause is not something a reload fixes (no
        // disk space, network down, ...), so stop hammering — repeated reloads
        // are expensive and pointless. The counter resets once the connection
        // reports healthy again (state == "ok").
        if (m_watchdogReloads >= 3) {
          if (!m_watchdogGaveUp) {
            m_watchdogGaveUp = true;
            qWarning() << "Connection watchdog: still stuck after 3 reloads, "
                          "giving up until the connection recovers.";
          }
          return;
        }

        ++m_watchdogReloads;
        m_lastWatchdogReload.restart();
        qWarning() << "Connection watchdog: WhatsApp WebSocket stuck, reload"
                   << m_watchdogReloads << "of 3.";
        if (m_webEngine)
          m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache);
      });
}

void MainWindow::handleLoadFinished(bool loaded) {
  if (loaded) {
    qDebug() << "Loaded";
    m_watchdogStrikes = 0; // fresh document, start clean
    checkLoadedCorrectly();
    updatePageTheme();
    handleZoom();
    if (m_settingsWidget != nullptr)
      m_settingsWidget->refresh();
  }
}

void MainWindow::checkLoadedCorrectly() {
  if (!m_webEngine || !m_webEngine->page())
    return;

  m_webEngine->page()->runJavaScript(
      "document.querySelector('body').className",
      [this](const QVariant &result) {
        if (result.toString().contains("page-version", Qt::CaseInsensitive)) {
          qDebug() << "Test 1 found" << result.toString();
          m_webEngine->page()->runJavaScript(
              "document.getElementsByTagName('body')[0].innerText = ''");
          loadingQuirk("test1");
        } else if (m_webEngine->title().contains("Error",
                                                 Qt::CaseInsensitive)) {
          Utils::delete_cache(m_webEngine->page()->profile()->cachePath());
          Utils::delete_cache(
              m_webEngine->page()->profile()->persistentStoragePath());
          SettingsManager::instance().settings().setValue("useragent",
                                                          defaultUserAgentStr);
          Utils::DisplayExceptionErrorDialog(
              "test1 handleWebViewTitleChanged(title) title: Error, "
              "Resetting UA, Quiting!\nUA: " +
              SettingsManager::instance()
                  .settings()
                  .value("useragent", "DefaultUA")
                  .toString());
          m_quitAction->trigger();
        } else {
          qDebug() << "Test 1 loaded correctly, value:" << result.toString();
        }
      });
}

void MainWindow::loadingQuirk(const QString &test) {
  if (m_correctlyLoadedRetries > -1) {
    qWarning() << test << "checkLoadedCorrectly()/loadingQuirk()/doReload()"
               << m_correctlyLoadedRetries;
    doReload(false, false, true);
    m_correctlyLoadedRetries--;
  } else {
    Utils::delete_cache(m_webEngine->page()->profile()->cachePath());
    Utils::delete_cache(
        m_webEngine->page()->profile()->persistentStoragePath());
    SettingsManager::instance().settings().setValue("useragent",
                                                    defaultUserAgentStr);
    Utils::DisplayExceptionErrorDialog(
        test +
        " checkLoadedCorrectly()/loadingQuirk() reload retries 0, Resetting "
        "UA, Quiting!\nUA: " +
        SettingsManager::instance()
            .settings()
            .value("useragent", "DefaultUA")
            .toString());
    m_quitAction->trigger();
  }
}

// ── Page theme ────────────────────────────────────────────────────────────────

void MainWindow::updatePageTheme() {
  if (!m_webEngine || !m_webEngine->page())
    return;

  const bool dark = SettingsManager::instance()
                        .settings()
                        .value("windowTheme", "light")
                        .toString() == "dark";

  // Sequence reverse-engineered from WhatsApp Web's own theme-toggle logic:
  //
  // 1. WA module calls via global require():
  //      WAWebUserPrefsGeneral.setSystemThemeMode(false)  -- disable "follow OS"
  //      WAWebUserPrefsGeneral.setTheme(theme)            -- persist preference
  //      WAWebThemeContext.applyThemeToUI(theme)          -- repaint UI
  //      WAWebSystemTheme.theme = theme                   -- update system ref
  //
  // 2. React class component setState -- walk fiber ancestors of .app-wrapper-web
  //    upward via .return until we find the component whose state has both
  //    .theme and .systemThemeMode, then setState({theme, systemThemeMode:false}).
  //    Fall back to forceUpdate() on ancestors if that component is not found.
  //
  // 3. DOM attributes + localStorage -- persists across reloads, covers
  //    any CSS-only observers.
  const QString js = QString(R"js(
    (function(theme) {
      var isDark = theme === 'dark';

      // ── 1. WA module calls via global require() ───────────────────────────
      if (typeof require === 'function') {
        try {
          var up = require('WAWebUserPrefsGeneral');
          if (up) {
            if (typeof up.setSystemThemeMode === 'function') up.setSystemThemeMode(false);
            if (typeof up.setTheme          === 'function') up.setTheme(theme);
          }
        } catch (e) {}
        try {
          var tc = require('WAWebThemeContext');
          if (tc && typeof tc.applyThemeToUI === 'function') tc.applyThemeToUI(theme);
        } catch (e) {}
        try {
          var st = require('WAWebSystemTheme');
          if (st) st.theme = theme;
        } catch (e) {}
      }

      // ── 2. React class component setState (upward fiber walk) ─────────────
      try {
        var wrapper = document.querySelector('.app-wrapper-web');
        var rk = wrapper && Object.keys(wrapper).find(function(k) {
          return k.startsWith('__reactFiber') || k.startsWith('__reactInternalInstance');
        });
        if (rk) {
          var fiber = wrapper[rk];
          var found = false;
          while (fiber) {
            var sn = fiber.stateNode;
            if (sn && sn.state &&
                sn.state.theme !== undefined &&
                sn.state.systemThemeMode !== undefined &&
                typeof sn.setState === 'function') {
              sn.setState({theme: theme, systemThemeMode: false});
              found = true;
              break;
            }
            fiber = fiber.return;
          }
          if (!found) {
            fiber = wrapper[rk];
            var count = 0;
            while (fiber && count < 10) {
              if (fiber.stateNode && typeof fiber.stateNode.forceUpdate === 'function') {
                try { fiber.stateNode.forceUpdate(); } catch (e) {}
                count++;
              }
              fiber = fiber.return;
            }
          }
        }
      } catch (e) {}

      // ── 3. DOM attributes + localStorage ─────────────────────────────────
      var root = document.documentElement;
      root.setAttribute('data-theme',      theme);
      root.setAttribute('data-color-mode', theme);
      root.style.colorScheme = theme;
      document.body.classList.toggle('dark', isDark);

      localStorage.setItem('theme', theme);
      localStorage.removeItem('system-theme-mode');
      try {
        window.dispatchEvent(new StorageEvent('storage', {
          key: 'theme', newValue: theme,
          storageArea: localStorage, url: location.href
        }));
      } catch (e) {}
    })('%1');
  )js").arg(dark ? "dark" : "light");

  m_webEngine->page()->runJavaScript(js);
}

QString MainWindow::getPageTheme() const {
  static QString theme = "web"; // implies light
  if (m_webEngine && m_webEngine->page()) {
    // Read back from the same localStorage key WhatsApp writes to, so we
    // always get the authoritative value regardless of DOM structure changes.
    m_webEngine->page()->runJavaScript(
        "(function(){"
        "  var v = localStorage.getItem('theme');"
        "  if (v === null) return '';"              // WhatsApp has not stored one
        "  try { v = JSON.parse(v); } catch(e) {}"  // handle both 'dark' and '"dark"'
        "  if (v === 'dark' || v === 'light') return v;"
        "  return '';"                              // anything else: no opinion
        "})();",
        [=](const QVariant &result) {
          const QString value = result.toString();
          // Only persist a theme the page actually reported. This ran on the
          // way out, while the page is being torn down, and runJavaScript then
          // calls back with an empty result — which the old code folded into
          // "light" and saved as if the user had chosen it. Every clean exit
          // therefore reset the theme, and the loss only showed up on the next
          // launch, which is why it looked like a startup bug.
          if (value != QLatin1String("dark") && value != QLatin1String("light"))
            return;
          theme = value;
          SettingsManager::instance().settings().setValue("windowTheme", theme);
        });
  }
  return theme;
}

// ── Fullscreen ────────────────────────────────────────────────────────────────

void MainWindow::fullScreenRequested(QWebEngineFullScreenRequest request) {
  if (request.toggleOn()) {
    windowStateBeforeFullScreen = this->windowState();
    this->hide();
    m_webEngine->showFullScreen();
    m_webEngine->setWindowState(Qt::WindowFullScreen);
    this->setWindowState(Qt::WindowFullScreen);
    this->show();
  } else {
    this->hide();
    m_webEngine->showNormal();
    this->setWindowState(windowStateBeforeFullScreen);
    this->show();
  }
  request.accept();
}

// ── Misc web engine helpers ───────────────────────────────────────────────────

void MainWindow::toggleMute(const bool &checked) {
  // Mute every account, not just the visible one — a background account's call
  // tone or notification sound should go quiet too.
  for (const Account &account : m_accounts)
    if (account.view && account.view->page())
      account.view->page()->setAudioMuted(checked);

  SettingsManager::instance().settings().setValue("muteAudio", checked);
  // Keep the tray action and the Settings checkbox showing the same state,
  // wherever the toggle came from.
  if (m_muteAction && m_muteAction->isChecked() != checked)
    m_muteAction->setChecked(checked);
  if (m_settingsWidget)
    m_settingsWidget->muteAudioSetChecked(checked);
}
