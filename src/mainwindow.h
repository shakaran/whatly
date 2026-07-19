#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWebChannel>
#include <QTimer>

class WebView;
class QTabBar;
class QStackedWidget;
class GlobalShortcut;
class ScheduledMessages;

#include "autolockeventfilter.h"
#include "downloadmanagerwidget.h"
#include "lock.h"
#include "notificationpopup.h"
#include "webenginenotifproxy.h"

#include <QHash>

class PortalNotification;
#include "settingswidget.h"
#include "pagebridge.h"
#include "webenginepage.h"
#ifdef Q_OS_LINUX
#include <libnotify-qt.h>
#include <QDBusVariant>
#endif

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

  void loadSchemaUrl(const QString &arg);
  void alreadyRunning();
  void runMinimized();
  void showNotification(QString title, QString message);
  void doReload(bool byPassCache = false, bool isAskedByCLI = false,
                bool byLoadingQuirk = false);

public slots:
  void updateWindowTheme();
  void applySystemThemeIfEnabled();
  void updatePageTheme();
  void handleWebViewTitleChanged(const QString &title);
  void handleLoadFinished(bool loaded);
  void showSettings(bool isAskedByCLI = false);
  void showAbout();
  void showScheduledMessages();
  void lockApp();
  void lockOnHideIfEnabled();
  void toggleTheme();
  void togglePrivacyBlur();
  void newChat();

protected slots:
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void changeEvent(QEvent *e) override;

private:
  const QIcon getTrayIcon(const int &notificationCount) const;
  // Qt's saveGeometry() cannot be trusted while the window is maximized (see
  // saveWindowGeometry), so the normal geometry is tracked by hand.
  void trackNormalGeometry();
  void saveWindowGeometry();
  QRect m_normalGeometry;
  QTimer *m_normalGeometryTimer = nullptr;
  bool m_restoreMaximized = false;
  bool m_geometryRestored = false;
  void createActions();
  void createTrayIcon();
  void createWebEngine();
  QString getPageTheme() const;
  void doAppReload();
  void askToReloadPage();
  void updateSettingsUserAgentWidget();
  void createWebPage(bool offTheRecord = false);
  // Loads WhatsApp into a specific account's view and profile. createWebPage is
  // the old single-account entry point and now just calls this for the active
  // account.
  void createPageFor(WebView *view, const QString &accountId);
  // Lets buttons Whatly injects into WhatsApp's UI call back into the app.
  void installPageBridge(QWebEnginePage *page);

  // ── In-window accounts (tabs) ─────────────────────────────────────────────
  // Each account is a separate WhatsApp session in its own view and profile,
  // switched by a tab bar that hides itself when only the default account
  // exists — so a single-account setup looks and behaves exactly as before.
  struct Account {
    QString id;      // "" for the default account, a random slug otherwise
    QString name;    // shown on the tab
    WebView *view = nullptr;
    int unread = 0;
  };
  // How the account views are laid out. Tabs (the historical default) shows one
  // account at a time behind a tab bar; Grid shows every account at once in a
  // tiled grid. Separate windows remain available via the --profile mechanism
  // (a second process), unaffected by this.
  enum class ViewMode { Tabs = 0, Grid = 1 };
  void buildAccountArea();
  void setViewMode(ViewMode mode);
  ViewMode viewMode() const { return m_viewMode; }
  void relayoutGrid();
  WebView *addAccount(const QString &id, const QString &name, bool load);
  void setActiveAccount(int index);
  void promptAddAccount();
  void renameAccount(int index);
  void removeAccount(int index);
  void saveAccounts();
  void loadAccounts();
  int accountIndexForView(const QObject *view) const;
  void refreshAccountTabs();
  void updateTrayUnread();
  // Emit the unread total as a taskbar badge via the com.canonical.Unity
  // LauncherEntry D-Bus protocol (read by KDE Plasma, Dash-to-Dock and others).
  void updateLauncherBadge(int count);

  QList<Account> m_accounts;
  int m_activeAccount = 0;
  QTabBar *m_accountBar = nullptr;
  QStackedWidget *m_accountStack = nullptr;
  // Grid view: a container the account views are re-parented into when the grid
  // mode is active. m_displayStack flips between the tabbed stack and the grid.
  QStackedWidget *m_displayStack = nullptr;
  QWidget *m_gridContainer = nullptr;
  ViewMode m_viewMode = ViewMode::Tabs;
  QAction *m_viewTabsAction = nullptr;
  QAction *m_viewGridAction = nullptr;
  void initSettingWidget();
  void tryLock();
  void ensureLockVisible();
  void checkLoadedCorrectly();
  void loadingQuirk(const QString &test);
  void checkConnectionHealth();
  void setNotificationPresenter(QWebEngineProfile *profile);
#ifdef Q_OS_LINUX
  Notification::EventPtr notify(const QString& title, const QString& body, qint32 timeout);
  static QVariant notificationImageHint(const QPixmap &pixmap);
#endif
  void initRateWidget();
  void handleZoomOnWindowStateChange(const QWindowStateChangeEvent *ev);
  void handleZoom();
  void zoomBy(double delta);
  void zoomIn();
  void zoomOut();
  void zoomReset();
  void applyMinimumSize();
  static constexpr int kBaseMinWidth = 525;
  static constexpr int kBaseMinHeight = 448;
  void forceLogOut();
  bool isLoggedIn();
  void tryLogOut();
  void initAutoLock();
  void triggerNewChat(const QString &phone, const QString &text);
  void restoreMainWindow();

#ifdef Q_OS_LINUX
  Notification::Manager m_notifier;
#else
  // Routes QSystemTrayIcon::messageClicked to the most recent notification
  QMetaObject::Connection m_trayNotificationClickConnection;
#endif
  QIcon m_trayIconNormal;
  QRegularExpression m_notificationsTitleRegExp;
  QRegularExpression m_unreadMessageCountRegExp;
  DownloadManagerWidget m_downloadManagerWidget;
  QScopedPointer<QWebEngineProfile> m_otrProfile;
  int m_correctlyLoadedRetries = 4;
  // Set while quitApp() runs so closeEvent() does not turn an intentional
  // quit into minimize-to-tray (Qt 6.3+ quit() closes windows first and a
  // vetoed close cancels the quit).
  bool m_isQuitting = false;

  // System-wide "raise the window" hotkey (Ctrl+Alt+W). X11 only; null/inactive
  // on Wayland, where a `whatly -w` desktop shortcut is the alternative.
  GlobalShortcut *m_globalShortcut = nullptr;

  // Connection watchdog: polls the injected WebSocket health probe and reloads
  // the page when WhatsApp's socket has died or gone silent (aggressive mode).
  QTimer *m_connectionWatchdog = nullptr;
  QElapsedTimer m_lastWatchdogReload;
  int m_watchdogStrikes = 0;
  int m_watchdogReloads = 0;      // reloads in the current hang episode (capped at 3)
  bool m_watchdogGaveUp = false;  // true once the cap is hit; reset on recovery
  bool m_trayConnected = true;    // reflected in the tray icon (see getTrayIcon)

  QAction *m_reloadAction = nullptr;
  QAction *m_minimizeAction = nullptr;
  QAction *m_restoreAction = nullptr;
  QAction *m_aboutAction = nullptr;
  QAction *m_settingsAction = nullptr;
  QAction *m_scheduledMessagesAction = nullptr;
  QAction *m_toggleThemeAction = nullptr;
  QAction *m_quitAction = nullptr;
  QAction *m_lockAction = nullptr;
  QAction *m_muteAction = nullptr;
  QAction *m_fullscreenAction = nullptr;
  QAction *m_openUrlAction = nullptr;
  QAction *m_zoomInAction = nullptr;
  QAction *m_zoomOutAction = nullptr;
  QAction *m_zoomResetAction = nullptr;

  QMenu *m_trayIconMenu = nullptr;
  QSystemTrayIcon *m_systemTrayIcon = nullptr;
  QWebEngineView *m_webEngine = nullptr;
  PageBridge *m_pageBridge = nullptr;
  QWebChannel *m_webChannel = nullptr;
  ScheduledMessages *m_scheduledMessages = nullptr;
  SettingsWidget *m_settingsWidget = nullptr;
  Lock *m_lockWidget = nullptr;
  AutoLockEventFilter *m_autoLockEventFilter = nullptr;
  Qt::WindowStates windowStateBeforeFullScreen;

  QString userDesktopEnvironment = Utils::detectDesktopEnvironment();

  void notificationClicked();
  NotificationPopup *m_webengine_notifier_popup = nullptr;

  // XDG-portal notification backend (Flatpak-friendly). Created lazily; the
  // active WhatsApp notifications are tracked by portal id so an activation can
  // be routed back to the right QWebEngineNotification.
  PortalNotification *m_portalNotifier = nullptr;
  QHash<QString, WebEngineNotifProxyPtr> m_portalProxies;
  quint64 m_portalNotifSeq = 0;
  // Whether the portal backend should be used for this run (from settings +
  // availability). Resolved once, lazily.
  bool usePortalNotifications();
private slots:
  void iconActivated(QSystemTrayIcon::ActivationReason reason);
  void toggleMute(const bool &checked);
  void fullScreenRequested(QWebEngineFullScreenRequest request);
  void checkWindowState();
  void initLock();
#ifdef Q_OS_LINUX
  // The freedesktop appearance portal changed a setting; re-apply the
  // system theme if we are following it.
  void onPortalSettingChanged(const QString &nspace, const QString &key,
                              const QDBusVariant &value);
#endif
  void quitApp();
  void changeLockPassword();
  void appAutoLockChanged();
};

#endif // MAINWINDOW_H
