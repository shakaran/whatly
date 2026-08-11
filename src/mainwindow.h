#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWebChannel>
#include <QDateTime>
#include <QTimer>

class WebView;
class QTabBar;
class QStackedWidget;
class QScrollArea;
class QSplitter;
class GlobalShortcut;
class ScheduledMessages;
class DetachedAccountWindow;
class AccountTabBar;
class LocalApiServer;

#include "messaging.h"

#include "autolockeventfilter.h"
#include "downloadmanagerwidget.h"
#include "lock.h"
#include "notificationpopup.h"
#include "webenginenotifproxy.h"

#include <QHash>
#include <QSet>
#include <QPointer>
#include <functional>

class PortalNotification;
class QLabel;
class Translator;
class NotificationReply;
class AiClient;
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
  // Open WhatsApp Web's "Join group" preview for an invite code (issue #186).
  void openGroupInvite(const QString &code);
  void alreadyRunning();
  void runMinimized();
  void showNotification(QString title, QString message);
  void doReload(bool byPassCache = false, bool isAskedByCLI = false,
                bool byLoadingQuirk = false);

public slots:
  void updateWindowTheme();
  void applySystemThemeIfEnabled();
  // Push the chosen theme into EVERY account's page. WhatsApp Web keeps its own
  // theme preference per profile, so a page nobody has told is left on whatever
  // it last stored — light, for a profile that has never been told anything.
  void updatePageTheme();
  // One page, for the paths that know which one they mean: the account that has
  // just finished loading, rather than whichever one happens to be on screen.
  void applyPageTheme(QWebEnginePage *page);
  void handleWebViewTitleChanged(const QString &title);
  void handleLoadFinished(bool loaded);
  void showSettings(bool isAskedByCLI = false);
  void showAbout();
  void showScheduledMessages();
  // Perform a message send requested over IPC by a `whatly --send …` command
  // (issue: send by command / API). Returns to the caller immediately; the send
  // itself runs asynchronously through the page automation.
  void commandSend(const Messaging::SendCommand &cmd);
  void lockApp();
  void lockOnHideIfEnabled();
  void toggleTheme();
  void togglePrivacyBlur();
  void toggleChatListStrip();
  // Keep that action's text saying what it will do next, for the palette.
  void refreshChatListStripAction();
  // Put the keyboard into WhatsApp Web's own chat search, in the account the
  // window in front is showing. Expands the chat list first when it is
  // collapsed, since the search box is clipped to a sliver there and focusing
  // something invisible is not a usable answer to the key.
  void focusChatSearch();
  // Relaunch this same executable with this same command line, so the settings
  // that only apply at startup take effect without the user having to quit and
  // find Whatly again. Everything about how the desk looks is saved first and
  // put back by the new process.
  void restartApp();
  // Bring the window up and give it focus. The tray menu uses it: an action
  // picked from there used to run with the window still behind everything.
  void raiseWindow();
  // The window the user should be brought to, which is simply the last one they
  // touched. There is deliberately no "main" window from the user's side: this
  // one owns the tray icon and the account list, but that is an implementation
  // detail, and a tray click, a notification or a global shortcut must never haul
  // it out from behind the window actually being worked in.
  QWidget *frontWindow() const;
  // Show, unminimise, raise and activate one window — and nothing else.
  static void bringForward(QWidget *w);
  // Every Whatly window, so "hide to tray" takes the whole app away rather than
  // just this one, which would be another way of showing which is special.
  QList<QWidget *> allWindows() const;
  // The same windows, most-recently-used first. Hiding is what makes the order
  // matter: it takes the whole app away, so bringing it back has to bring all of
  // it back, and in the order the user left it.
  QList<QWidget *> windowsByFocus() const;
  // Bring the whole app back: every window that is hidden or minimised, with the
  // one last used raised on top. Showing only that one is what stranded the
  // others — hidden, with no window left to click and no entry pointing at them.
  void restoreAllWindows();
  // Put the whole app in the tray: every window, for the same reason hiding one
  // and leaving the rest would say one of them is the real one.
  void hideAllWindows();
  void newChat();
  // Whether the account strip stays up with only one account, where it is a row
  // of chrome carrying a single tab. Off by default; the "+" it holds is also
  // on Ctrl+K, the command palette and the Add-account action.
  static bool alwaysShowAccountTabs();
  static void setAlwaysShowAccountTabs(bool enabled);
  // What the unread badge counts. Three answers, because the right one is a
  // matter of how someone uses WhatsApp rather than of fact: muted chats still
  // show a pill in the list (so they are counted by default), archived ones are
  // the pile deliberately put away (so they are not), and the badge counts
  // conversations rather than messages unless asked otherwise — a sum is
  // dominated by whichever group is busiest, and every chat already shows its
  // own number.
  static bool unreadCountIncludesMuted();
  static void setUnreadCountIncludesMuted(bool enabled);
  static bool unreadCountIncludesArchived();
  static void setUnreadCountIncludesArchived(bool enabled);
  static bool unreadCountCountsMessages();
  static void setUnreadCountCountsMessages(bool enabled);
  // Count again everywhere, now — for when one of those three changes and the
  // number on screen means something different from the moment it is ticked.
  void countUnreadEverywhere();
  int accountCount() const { return m_accounts.size(); }
  // Re-run the strip's visibility and labels after that setting changes.
  void refreshAccountStrip();

protected slots:
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void changeEvent(QEvent *e) override;

protected:
  // Watches the grid scroll area's viewport so the grid container is kept at
  // max(viewport, whole-grid-minimum) — the scroll area then owns the only
  // scrollbars, and tiles never shrink small enough to grow their own.
  bool eventFilter(QObject *watched, QEvent *event) override;

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
    // WhatsApp Web version (window.Debug.VERSION), captured on load and shown in
    // the tab tooltip. Empty until the page reports it.
    QString waVersion;
    // When this account was last the active/visible one; drives idle suspension.
    QDateTime lastActive;
    // Whether this account currently has a page. A dormant account has none at
    // all: not a frozen one, not an empty one. It gets a page the first time it
    // is opened, and loses it again once it has been idle long enough.
    bool loaded = false;
    // Whether that page has finished loading WhatsApp Web. `loaded` says a page
    // exists; this says there is something on it to act on. A page built for a
    // tray pick has neither a chat list nor the chat being asked for until this
    // is true.
    bool ready = false;
    // The unread chats this account last reported, kept so the tray can still
    // offer them once the account has no page. A dormant account is the one that
    // most needs a way back into it, and dropping its chats takes that away.
    QList<QPair<QString, int>> recentUnread;
    // Non-null while the account has been torn off into its own window; its
    // view then lives in that window rather than in the tab stack/grid.
    QPointer<DetachedAccountWindow> window = nullptr;
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
  void clearGridCells();
  void updateGridCaptions();
  WebView *addAccount(const QString &id, const QString &name, bool load);
  // The account's live page, or nullptr while it is dormant. Everything that
  // walks the account list must go through this rather than view->page():
  // QWebEngineView hands out a page on demand, so asking a dormant account for
  // one would build it — on the default profile, and defeating the point.
  static QWebEnginePage *pageOf(const Account &a);
  // Throw an idle account's page away, back to the dormant state it started in.
  // Reopening the account builds it again from scratch.
  void unloadAccount(int index);
  // Build a dormant account's page because it is about to be shown. Every path
  // that puts an account on screen has to call this, not just the tab switch in
  // the main window — a detached window shows its own account without going
  // anywhere near setActiveAccount().
  void ensureAccountLoaded(int index);
  // Rebuild the native surface of a view that has just been moved into another
  // window. Qt can leave it behind, which is what made an account torn into its
  // own window come up black, with docking and tearing out again the only cure.
  // One-shot per view: the hide/show it does causes another Show event, which
  // arrives back in eventFilter().
  void nudgeReparentedView(int index);
  QSet<QWidget *> m_nudgedViews;
  void setActiveAccount(int index);
  // `target` is the detached window whose "+" was clicked, so the new account
  // lands where it was asked for. Null means the main strip, which falls back to
  // the focused window for the tray and palette paths.
  void promptAddAccount(DetachedAccountWindow *target = nullptr);
  void renameAccount(int index);
  void removeAccount(int index);
  // Tear an account off into its own top-level window. An account's `window`
  // field records which window hosts it (null = the main window); the view is
  // reparented between windows' stacks but never destroyed on a move.
  void detachAccount(int index, QPoint dropGlobalPos = QPoint(-1, -1));
  // Dock the account with `id` into the main window at slot `insertSlot` among
  // the main strip's tabs (reordering a docked tab, or dragging an account back
  // in from a detached window).
  void dockAccountToMainAt(const QString &id, int insertSlot);
  // Move the account with `id` into an existing detached window at `slot`
  // (targetWin must be non-null; the main window is dockAccountToMainAt's job).
  void moveAccountToWindow(const QString &id, DetachedAccountWindow *targetWin,
                           int slot);
  // The main window's last tab was dropped into `win`: rather than leave the
  // main window empty (or destroy it — it owns the tray), the main window
  // absorbs `win`, taking its geometry and all of its tabs with the dragged
  // account inserted at `slot`; `win` is then destroyed.
  void absorbWindowIntoMain(DetachedAccountWindow *win, const QString &movedId,
                            int slot);
  // Tear the account off into a brand-new window near `pos`, from anywhere.
  void tearOutToNewWindow(const QString &id, QPoint pos);
  // A tab drag ended: if it landed on a strip the strip already handled it; if
  // on another Whatly window's body, move it there (appended); otherwise tear
  // off a new window.
  void onTabDragReleased(const QString &id, QPoint globalPos);
  DetachedAccountWindow *createDetachedWindow();
  // Dock all of a closing window's accounts back into the main window.
  void closeDetachedWindow(DetachedAccountWindow *win);
  // Rebuild every detached window's tab strip from m_accounts.
  void refreshDetachedStrips();
  int accountIndexForId(const QString &id) const;
  // The account the user is actually looking at: the one shown by whichever
  // window has the keyboard, which is not m_activeAccount — switching tabs in a
  // detached window swaps that window's stack without touching the app-wide
  // "active" account. Any shared action triggered by a key has to ask this, or it
  // acts on whatever was last clicked in the main window. -1 if there is none.
  int focusedAccountIndex() const;
  // Most-recently-focused-first list of windows (nullptr = the main window). The
  // front is the "main" window: it receives newly-added accounts, and a closed
  // window's tabs dock into the front-most surviving window.
  QList<DetachedAccountWindow *> m_focusOrder;
  void noteWindowFocused(DetachedAccountWindow *win);
  void destroyDetachedWindow(DetachedAccountWindow *win);
  // Re-derive a window's account order in m_accounts from its strip's tab order
  // after the user slides a tab (win == nullptr means the main window).
  void reorderWindowFromStrip(DetachedAccountWindow *win);
  bool m_reorderingTabs = false; // guards the tabMoved handler against re-entry
  // Set when a strip's drop already handled a tab drag (positional dock/move),
  // so the geometry-routing dragReleased handler skips it instead of re-moving.
  bool m_tabDropHandledByStrip = false;
  // One-shot tip, the first time a second account appears, telling the user a
  // tab can be pulled out into its own window.
  void maybeShowDetachHint();
  void saveAccounts();
  void loadAccounts();
  // Persist / restore the full multi-window arrangement (which accounts sit in
  // which window, each detached window's geometry and active tab). The layout is
  // saved ALWAYS (on every tab move / geometry change); the "rememberWindowLayout"
  // toggle only decides whether restore rebuilds the windows or comes up
  // collapsed (tabs kept in ordinal order). A crash guard skips the rebuild once
  // after a run that did not settle cleanly, without discarding the layout.
  void saveWindowLayout();
  void restoreWindowLayout();
  // Reorder the tabs into "main first, then each non-main window in ordinal
  // order" for the collapsed (single-window) case.
  void collapseToOrdinalOrder(const QStringList &assign);
  bool m_loadingLayout = false; // guards saveAccounts while a layout is restored
  QTimer *m_layoutSaveTimer = nullptr; // debounces layout saves on window moves
  // How every window is titled: the account it is showing, with its unread count
  // when it has one — the same string that account's tab carries. Which window
  // is which is the only question a title has to answer, and it is the same
  // question whether the window holds one account or a strip of them. No
  // application name in it: Qt appends that itself for the system's title bar,
  // the task list and Alt-Tab, and CustomTitleBar puts it in front where the
  // frame is ours to draw.
  QString accountTitle(int idx) const;
  // The window in which this account is the one on screen, or nothing — which
  // is the question a title change has to ask, since an account sitting behind
  // another account's tab must not retitle the window in front of it.
  QWidget *windowShowingAccount(int idx) const;
  // Ask one account's page how much is unread and put it on the badges. The
  // page throttles the reading; this can be called as often as is useful.
  void countUnread(int idx);
  QTimer *m_unreadTimer = nullptr;
  int accountIndexForView(const QObject *view) const;
  void refreshAccountTabs();
  // Ask a freshly loaded account's page for its WhatsApp Web version and cache
  // it on the Account, then refresh the tab tooltips.
  void captureAccountVersion(WebView *view);
  // Freeze background account pages that have been idle past the configured
  // threshold, to cut memory (Performance setting; off by default).
  void suspendIdleAccounts();
  QTimer *m_suspendTimer = nullptr;
  // Recent unread chats in the tray menu (idea #3): refresh the cached list from
  // the active account and jump to one on click.
  QMenu *m_recentUnreadMenu = nullptr;
  void refreshRecentUnread();
  // Fill that menu in from what every account last reported, dormant ones
  // included. Separate from the scan above, which is rate-limited.
  void rebuildRecentUnreadMenu();
  QElapsedTimer m_recentUnreadScan;
  // What each entry in that menu points at (account id, chat name), in the same
  // order — the entries are reused, so this is how a click finds its chat.
  QList<QPair<QString, QString>> m_recentUnreadTargets;
  void openChatByName(const QString &accountId, const QString &name);
  // Every window in the tray menu, numbered by how recently it was used, so none
  // can be left with nothing pointing at it.
  QMenu *m_windowsMenu = nullptr;
  // What each entry in that menu points at, in the same order — so a click goes
  // to the window whose label was read, and to nothing at all if that window has
  // since closed.
  QList<QPointer<QWidget>> m_windowsMenuTargets;
  void refreshWindowsMenu();
  QString windowLabel(const QWidget *w) const;
  // A chat asked for before its account had a page: run it once that page
  // reports itself loaded. The name is the presence flag — the account id cannot
  // be, since the default account's id is the empty string.
  QString m_pendingChatAccount;
  QString m_pendingChatName;
  // The tab tooltip for an account: its WhatsApp Web version (once known) and
  // the build token from the page URL. Empty while neither is available.
  QString accountTabTooltip(const Account &acc) const;
  void updateTrayUnread();
  // Emit the unread total as a taskbar badge via the com.canonical.Unity
  // LauncherEntry D-Bus protocol (read by KDE Plasma, Dash-to-Dock and others).
  void updateLauncherBadge(int count);

  QList<Account> m_accounts;
  int m_activeAccount = 0;
  AccountTabBar *m_accountBar = nullptr;
  QStackedWidget *m_accountStack = nullptr;
  // Grid view: a container the account views are re-parented into when the grid
  // mode is active. m_displayStack flips between the tabbed stack and the grid.
  QStackedWidget *m_displayStack = nullptr;
  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_gridScroll = nullptr; // wraps the grid so tiles never clip
  // The grid is a vertical splitter of rows, each a horizontal splitter of
  // tiles, so the dividers between them can be dragged. Column widths are kept
  // in sync across rows so a drag resizes a whole column.
  QSplitter *m_gridVSplit = nullptr;
  QList<QPointer<QSplitter>> m_gridRowSplits;
  bool m_gridCustomized = false; // user dragged a divider or resized the grid
  bool m_gridSyncing = false;    // guards column-sync / setSizes re-entry
  bool m_gridResizing = false;   // guards our own programmatic grid resizes
  QRect m_gridSavedGeom;         // remembered grid window size (when customized)
  QList<int> m_gridSavedRows;    // remembered row heights (when customized)
  QList<int> m_gridSavedCols;    // remembered column widths (when customized)
  QSize m_gridMinSize;           // the whole grid at minimum tile size
  void syncGridContainerSize();  // resize the grid to max(viewport, m_gridMinSize)
  void resetGridTiles();         // distribute rows/cols equally + grow window
  void applyGridSizes();         // apply remembered sizes, or reset if they don't fit
  void captureGridSizes();       // snapshot current divider sizes + grid geometry
  void markGridCustomized();     // record a user divider/size change
  void syncGridColumns(QSplitter *source); // mirror one row's columns to the rest
  void growWindowForGrid();      // enlarge so each tile is >= the WebApp minimum
  QList<QPointer<QLabel>> m_gridLabels;
  // The window geometry before Grid grew the window to fit its tiles, restored
  // when Grid is left. Null when not in (a grown) Grid.
  QRect m_preGridGeometry;
  ViewMode m_viewMode = ViewMode::Tabs;
  QAction *m_viewTabsAction = nullptr;
  QAction *m_viewGridAction = nullptr;
  QAction *m_addAccountAction = nullptr;
  QAction *m_commandPaletteAction = nullptr;
  void showCommandPalette();

  // Inline translation (idea #6). Reads the current selection or the composer,
  // sends it to the configured LibreTranslate endpoint from C++, then shows the
  // result in a toast (selection) or replaces the composer text (outgoing).
  Translator *m_translator = nullptr;
  void translateSelection();
  void translateComposer();
  // Read `text` from the page, translate it, and either toast the result or put
  // it back into the composer. Empty/failed input is reported to the user.
  void runTranslation(const QString &text, bool intoComposer);
  QString appTargetLanguage() const;

  // Export the open conversation (idea #9): kick the page collector, poll its
  // progress, then write a .txt + .json + media/ folder into a chosen directory.
  QAction *m_exportChatAction = nullptr;
  QTimer *m_exportPollTimer = nullptr;
  void exportCurrentChat();
  // Reads the completed collector payload from the page and writes the
  // transcript, JSON and media files into a dated subfolder of `baseDir`.
  void writeChatExport(const QString &baseDir);

  // AI assistant (idea #5): summarise the open chat, improve the composer text,
  // or suggest a reply, through the configured OpenAI-compatible endpoint.
  AiClient *m_aiClient = nullptr;
  QAction *m_aiSummarizeAction = nullptr;
  QAction *m_aiImproveAction = nullptr;
  QAction *m_aiSuggestAction = nullptr;
  QAction *m_aiUnreadDigestAction = nullptr;
  QAction *m_aiFormalAction = nullptr;
  QAction *m_aiFriendlyAction = nullptr;
  QAction *m_aiShorterAction = nullptr;
  void aiSummarizeChat();
  void aiImproveComposer();
  void aiSuggestReply();
  // Rewrite the composer draft with the given system prompt (tone presets).
  void aiRewrite(const QString &systemPrompt);
  // Triage every unread chat into one prioritised digest, shown in a dialog.
  // Reads the unread rows (name + count + preview) without opening any chat.
  void aiSummarizeUnread();

  // Manual Do Not Disturb (idea #10): silence popups on demand, on top of the
  // scheduled window. The checkable action reflects "manual DND on"; the timed
  // ones snooze for a while. VIP/keyword still break through (see shouldNotify).
  QAction *m_dndAction = nullptr;      // checkable: on until turned off
  QAction *m_dnd1hAction = nullptr;
  QAction *m_dnd2hAction = nullptr;
  QAction *m_dndMorningAction = nullptr;
  QTimer *m_dndExpiryTimer = nullptr;
  void setDndManual(bool on);          // toggled by m_dndAction
  void dndSnoozeFor(int minutes);      // timed snooze
  void dndSnoozeUntilMorning();
  void refreshDndUi();                 // sync the checkable state + expiry timer
  void dndToast(const QString &message);

  // Reply reminders (snooze a chat): schedule a reminder for the open chat at
  // `when`; it fires a desktop notification and reopens the chat.
  QAction *m_remind1hAction = nullptr;
  QAction *m_remind3hAction = nullptr;
  QAction *m_remindTomorrowAction = nullptr;
  void scheduleChatReminder(const QDateTime &when);

  // Warn once at startup when the data folder's volume is nearly full (WhatsApp
  // Web's LevelDB corrupts on truncated writes), and offer to move the folder to
  // a roomier disk. Applied on the next launch via the storage/dataDir setting.
  void checkStorageSpace();
  void promptChangeDataDir();
  // The current WebEngine data folder: the storage/dataDir override, or default.
  QString currentDataDir() const;
  // Send system+user prompts and hand the result to `onResult`; progress and
  // errors are shown as an in-page toast (and a desktop notification on error,
  // so feedback is not lost if the window is unfocused). Guards on AI enabled.
  void runAssistant(const QString &systemPrompt, const QString &userPrompt,
                    std::function<void(const QString &)> onResult);
  // Put AI text into the composer, then verify it actually landed; if it did not
  // (e.g. the window was not focused when the reply arrived), show it in a dialog
  // so the result is never silently lost.
  void deliverAiText(const QString &text);
  // A simple read-only dialog for AI output (summary, or the fallback above).
  void showTextDialog(const QString &title, const QString &text);
  class UpdateChecker *m_updateChecker = nullptr;
  QString m_pendingUpdateUrl;
  void initSettingWidget();
  void tryLock();
  void ensureLockVisible();
  void checkLoadedCorrectly();
  // One-time notice when the engine lacks H.264/MP4 codecs (issue #34), so a
  // failed video attach is explained. Probed once per session via canPlayType.
  void checkMediaCodecs();
  bool m_codecCheckDone = false;
  void loadingQuirk(const QString &test);
  // Send a local file as an attachment to `number` through WhatsApp Web (used
  // by commandSend for `--send --file`). Best-effort page automation.
  void sendAttachmentViaWeb(const QString &number, const QString &path,
                            const QString &caption);
  // The page-side script that completes an attachment send (injected on load).
  static QString attachmentSenderScriptSource();
  // Send text to a contact/group given by name (or a group id) through WhatsApp
  // Web: opens the target chat by exact-title search, then types and sends.
  // Used by commandSend for a non-phone-number recipient. Best-effort.
  void sendByNameViaWeb(const Messaging::Recipient &recipient,
                        const QString &text,
                        const QString &filePath = QString());
  // The page-side script that opens a chat by name/id and sends (injected on
  // load).
  static QString nameSenderScriptSource();
  // Start (or stop) the opt-in local HTTP API according to the current
  // settings. Safe to call again after the settings change.
  void startLocalApi();
  // Handle an incoming message delivered by a Cloud API webhook: evaluate the
  // auto-reply rules and reply through the Cloud API if one matches.
  void handleCloudIncoming(const QString &from, const QString &text);
  // The page-side observer that reports new incoming messages over the bridge.
  static QString autoReplyObserverScriptSource();
  // Evaluate the auto-reply rules for an incoming message and, if one matches,
  // send the reply into the open conversation.
  void handleIncomingMessage(const QString &text);
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
  // Grid divider thickness; also the gap the grid's minimum size accounts for.
  static constexpr int kGridHandle = 6;
  // A grid tile is a glance pane, not a full workspace, so it does not borrow
  // the window minimum. It keeps the width WhatsApp Web wants before it
  // collapses the chat list, but only enough height to read recent messages —
  // the window minimum's 448 forces a 2-row grid past what a 1080p laptop can
  // show, which leaves the dividers with nothing to redistribute.
  static constexpr int kGridMinWidth = kBaseMinWidth;
  static constexpr int kGridMinHeight = 320;
  // Room beyond the grid minimum, so the dividers can be dragged as soon as
  // Grid view opens rather than only once the window is enlarged by hand.
  static constexpr int kGridSlack = 80;
  QSize gridTileMinSize() const;
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

  // Quick-compose overlay (idea #4), summoned by Ctrl+Alt+N or the tray/command
  // palette. Sends through the existing web path without opening the window.
  class QuickCompose *m_quickCompose = nullptr;
  void showQuickCompose();

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
  QAction *m_chatListStripAction = nullptr;
  QAction *m_findChatAction = nullptr;
  QAction *m_translateSelectionAction = nullptr;
  QAction *m_translateComposerAction = nullptr;

  QMenu *m_trayIconMenu = nullptr;
  QSystemTrayIcon *m_systemTrayIcon = nullptr;
  // Timestamp of the last time the window lost activation, for the tray-click
  // "was frontmost a moment ago" heuristic in iconActivated().
  qint64 m_lastDeactivationMs = 0;
  QWebEngineView *m_webEngine = nullptr;
  PageBridge *m_pageBridge = nullptr;
  QWebChannel *m_webChannel = nullptr;
  ScheduledMessages *m_scheduledMessages = nullptr;
  LocalApiServer *m_localApi = nullptr;
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

  // Inline-reply notification backend (idea #2). Created lazily; each posted
  // notification is tracked by its server id together with the chat it is for,
  // so a typed reply can be sent straight back to that chat.
  NotificationReply *m_notificationReply = nullptr;
  QHash<quint32, QPair<WebEngineNotifProxyPtr, QString>> m_replyNotifs;
  // Sets up m_notificationReply and its signal wiring on first use; returns true
  // when inline reply is enabled and available.
  bool ensureInlineReply();
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
  void onScreenSaverActiveChanged(bool active);
#endif
  void quitApp();
  void changeLockPassword();
  void appAutoLockChanged();
};

#endif // MAINWINDOW_H
