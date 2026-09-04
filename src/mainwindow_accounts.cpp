// In-window accounts: a tab bar over a stack of WhatsApp views, one per signed-
// in account. The tab bar hides itself when only the default account exists, so
// a single-account setup is untouched by any of this.
#include "mainwindow.h"

#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QApplication>
#include <QMenu>
#include <QStackedWidget>
#include <QPointer>
#include <QTabBar>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QWidget>
#include <QtMath>

#include "settingsmanager.h"
#include "customtitlebar.h"
#include "windowresizer.h"
#include "commandpalette.h"
#include "cannedresponses.h"

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>
#endif

#include "accounttabbar.h"
#include "appprofile.h"
#include "common.h"
#include "detachedaccountwindow.h"
#include "utils.h"
#include "chatnav.h"
#include "performance.h"
#include "webview.h"
#include <QAction>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebEnginePage>

#include <QTimer>

// The file `whatly --unread` reads: the current unread total for this account.
// Kept in the runtime dir (cleared on logout) with the profile suffix, so each
// --profile account has its own.
static QString unreadCountFile() {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty())
    dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  return dir + QStringLiteral("/whatly-unread") + AppProfile::suffix();
}

bool MainWindow::alwaysShowAccountTabs() {
  return SettingsManager::instance()
      .settings()
      .value(QStringLiteral("alwaysShowAccountTabs"), false)
      .toBool();
}

void MainWindow::setAlwaysShowAccountTabs(bool enabled) {
  SettingsManager::instance().settings().setValue(
      QStringLiteral("alwaysShowAccountTabs"), enabled);
}

bool MainWindow::unreadCountIncludesMuted() {
  return SettingsManager::instance()
      .settings()
      .value(QStringLiteral("unreadCountIncludesMuted"), true)
      .toBool();
}

void MainWindow::setUnreadCountIncludesMuted(bool enabled) {
  SettingsManager::instance().settings().setValue(
      QStringLiteral("unreadCountIncludesMuted"), enabled);
}

bool MainWindow::unreadCountIncludesArchived() {
  return SettingsManager::instance()
      .settings()
      .value(QStringLiteral("unreadCountIncludesArchived"), false)
      .toBool();
}

void MainWindow::setUnreadCountIncludesArchived(bool enabled) {
  SettingsManager::instance().settings().setValue(
      QStringLiteral("unreadCountIncludesArchived"), enabled);
}

bool MainWindow::unreadCountCountsMessages() {
  return SettingsManager::instance()
      .settings()
      .value(QStringLiteral("unreadCountCountsMessages"), false)
      .toBool();
}

void MainWindow::setUnreadCountCountsMessages(bool enabled) {
  SettingsManager::instance().settings().setValue(
      QStringLiteral("unreadCountCountsMessages"), enabled);
}

void MainWindow::countUnread(int idx) {
  if (idx < 0 || idx >= m_accounts.size())
    return;
  QWebEnginePage *page = pageOf(m_accounts[idx]);
  if (!page)
    return; // dormant: it has nothing to count with
  const QString id = m_accounts[idx].id;
  page->runJavaScript(
      ChatNav::unreadSummaryScript(unreadCountIncludesMuted(),
                                   unreadCountIncludesArchived()),
      [this, id](const QVariant &result) {
        const int i = accountIndexForId(id);
        if (i < 0)
          return;
        // A count that could not be taken must leave the badge as it is. Zero is
        // an answer — nothing is unread — and it has to be told apart from a
        // page that was reloading, a store that would not open and a script that
        // returned something unexpected, or the badge clears itself every time
        // one of those happens, which during a reload is every time.
        QJsonParseError parse{};
        const QJsonDocument doc =
            QJsonDocument::fromJson(result.toString().toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject())
          return;
        const QJsonObject o = doc.object();
        const QJsonValue counted = o.value(QStringLiteral("chats"));
        if (o.value(QStringLiteral("source")).toString() ==
                QLatin1String("none") ||
            !counted.isDouble() || counted.toInt() < 0)
          return;
        const int chats = counted.toInt();
        const int messages = o.value(QStringLiteral("messages")).toInt();
        const int shown = unreadCountCountsMessages() ? messages : chats;

        // The detail behind the badge, for the tray tooltip. The muted split comes
        // from the database walk only; the fallback that counts drawn rows cannot
        // see it, and its absence must not read as "none are muted".
        UnreadBreakdown detail;
        detail.chats = chats;
        detail.messages = messages;
        detail.mutedKnown = o.contains(QStringLiteral("mutedChats"));
        detail.mutedChats = o.value(QStringLiteral("mutedChats")).toInt();
        detail.mutedMessages = o.value(QStringLiteral("mutedMessages")).toInt();
        // Whether chats/messages already count the muted ones, so the tooltip can
        // add them back for the real total when the badge is set to leave them out.
        detail.mutedInTotal = unreadCountIncludesMuted();
        const bool detailMoved =
            m_accounts[i].unreadDetail.messages != detail.messages ||
            m_accounts[i].unreadDetail.chats != detail.chats ||
            m_accounts[i].unreadDetail.mutedChats != detail.mutedChats ||
            m_accounts[i].unreadDetail.mutedMessages != detail.mutedMessages;
        m_accounts[i].unreadDetail = detail;

        if (m_accounts[i].unread == shown) {
          // The badge is unchanged, but the tooltip's detail may not be: unread
          // messages arriving in muted chats move nothing else.
          if (detailMoved)
            updateTrayUnread();
          return;
        }
        m_accounts[i].unread = shown;
        refreshAccountTabs();
        updateTrayUnread();
      });
}

void MainWindow::countUnreadEverywhere() {
  for (int i = 0; i < m_accounts.size(); ++i)
    countUnread(i);
}

void MainWindow::refreshAccountStrip() { refreshAccountTabs(); }

void MainWindow::buildAccountArea() {
  m_focusOrder.append(nullptr); // the main window starts as the focused ("main") one

  // Debounced layout save: window moves/resizes fire rapidly during a drag, so
  // coalesce them into a single write after the motion settles.
  m_layoutSaveTimer = new QTimer(this);
  m_layoutSaveTimer->setSingleShot(true);
  m_layoutSaveTimer->setInterval(500);
  connect(m_layoutSaveTimer, &QTimer::timeout, this,
          [this]() { saveWindowLayout(); });

  // Periodically freeze idle background accounts to save memory (opt-in). The
  // check is cheap and a no-op unless the setting is on.
  m_suspendTimer = new QTimer(this);
  m_suspendTimer->setInterval(60 * 1000);
  connect(m_suspendTimer, &QTimer::timeout, this,
          [this]() { suspendIdleAccounts(); });
  m_suspendTimer->start();

  // Unloading a window's accounts once it has been away for the configured delay
  // (issue #25). The interval is set each time a window goes, since it is that
  // delay and the user can change it; the sweep above is what catches the rest.
  m_offscreenTimer = new QTimer(this);
  m_offscreenTimer->setSingleShot(true);
  connect(m_offscreenTimer, &QTimer::timeout, this, [this]() {
    unloadOffscreenWindowAccounts();
    // One timer, and it has just been spent. Any window that is away but not yet
    // past its own wait would otherwise be left with nothing to come back for
    // until the next sweep, so the next deadline is armed here (issue #91).
    armOffscreenTimer();
  });

  // A title change is not the only way an unread count moves: marking a chat
  // read or unread by hand moves it without one, and so does reading a chat on
  // the phone. The page throttles the work — a call between reads simply hands
  // back the last number — so this costs a function call and a string per
  // account every few seconds.
  m_unreadTimer = new QTimer(this);
  m_unreadTimer->setInterval(3 * 1000);
  connect(m_unreadTimer, &QTimer::timeout, this,
          [this]() { countUnreadEverywhere(); });
  m_unreadTimer->start();
  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Optional client-side title bar (frameless mode). Sits above everything —
  // unless the tabs are to share its row, in which case it is built further
  // down, beside them.
  const bool tabsInTitleBar = CustomTitleBar::tabsInTitleBar();
  if (CustomTitleBar::isEnabled() && !tabsInTitleBar)
    layout->addWidget(new CustomTitleBar(this, central));

  m_accountBar = new AccountTabBar(central);
  m_accountBar->setObjectName("accountBar");
  m_accountBar->setExpanding(false);
  m_accountBar->setDrawBase(false);
  m_accountBar->setFocusPolicy(Qt::NoFocus);
  m_accountBar->setContextMenuPolicy(Qt::CustomContextMenu);

  m_accountStack = new QStackedWidget(central);

  QSizePolicy expanding(QSizePolicy::Expanding, QSizePolicy::Expanding);
  expanding.setHorizontalStretch(1);
  expanding.setVerticalStretch(1);
  m_accountStack->setSizePolicy(expanding);

  // The grid container holds every account view at once when the grid mode is
  // active. The display stack flips between the tabbed stack (page 0) and the
  // grid (page 1); the account views are re-parented between the two on switch.
  m_gridContainer = new QWidget;
  auto *gbox = new QVBoxLayout(m_gridContainer);
  gbox->setContentsMargins(0, 0, 0, 0);
  gbox->setSpacing(0); // relayoutGrid fills this with the row/column splitters
  // A scroll area guarantees no tile is ever clipped/hidden: when the window is
  // too small to show every account at its usable minimum, thin scrollbars span
  // the grid instead of dropping tiles off the bottom.
  m_gridScroll = new QScrollArea(central);
  // NOT widget-resizable: we size the container ourselves (syncGridContainerSize)
  // to max(viewport, whole-grid-minimum), so the scroll area owns the only
  // scrollbars and no tile is ever shrunk into growing its own.
  m_gridScroll->setWidgetResizable(false);
  m_gridScroll->setFrameShape(QFrame::NoFrame);
  m_gridScroll->setStyleSheet(QStringLiteral(
      "QScrollBar:vertical{width:8px;margin:0;}"
      "QScrollBar:horizontal{height:8px;margin:0;}"
      "QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}"));
  m_gridScroll->setWidget(m_gridContainer);
  m_gridScroll->viewport()->installEventFilter(this); // track viewport resizes

  m_displayStack = new QStackedWidget(central);
  m_displayStack->setSizePolicy(expanding);
  m_displayStack->addWidget(m_accountStack);   // page 0: tabs
  m_displayStack->addWidget(m_gridScroll);     // page 1: grid (scrollable)

  if (tabsInTitleBar) {
    // Chrome-style: the tab strip IS the title bar, which buys back the whole
    // 34px the separate bar was costing. The two stay separate widgets on
    // purpose — Grid view hides the strip, and a window still needs its
    // minimise/maximise/close buttons when it does.
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(0);
    titleRow->addWidget(m_accountBar, 0);
    titleRow->addWidget(
        new CustomTitleBar(this, central, CustomTitleBar::Mode::Merged), 1);
    layout->addLayout(titleRow);
  } else {
    layout->addWidget(m_accountBar);
  }
  layout->addWidget(m_displayStack);

  // In frameless mode there is no native resize edge. This used to be a single
  // QSizeGrip in the bottom-right corner — one grab region out of the eight a
  // normal window has. WindowResizer restores all eight; the margin below is the
  // border strip it watches, and it has to belong to `central` rather than to any
  // child, or the web view would swallow the mouse events.
  if (CustomTitleBar::isEnabled()) {
    layout->setContentsMargins(WindowResizer::kBorder, WindowResizer::kBorder,
                               WindowResizer::kBorder, WindowResizer::kBorder);
    WindowResizer::install(central, this);
  }

  setCentralWidget(central);

  m_accountBar->setAcceptDrops(true); // the main strip accepts dropped tabs

  // Each account tab stores its stable account id in tab data; the "+" tab has
  // no data. Detached accounts live in their own windows, not on this strip.
  connect(m_accountBar, &QTabBar::currentChanged, this, [this](int tabIndex) {
    if (tabIndex < 0)
      return;
    const QVariant data = m_accountBar->tabData(tabIndex);
    if (!data.isValid()) { // the "+" affordance
      promptAddAccount();
      return;
    }
    const int index = accountIndexForId(data.toString());
    if (index >= 0)
      setActiveAccount(index);
  });
  connect(m_accountBar, &QTabBar::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            const int tabIndex = m_accountBar->tabAt(pos);
            if (tabIndex < 0)
              return;
            const QVariant data = m_accountBar->tabData(tabIndex);
            if (!data.isValid())
              return; // the "+" tab
            const int index = accountIndexForId(data.toString());
            if (index < 0)
              return;
            int docked = 0;
            for (const Account &a : m_accounts)
              if (!a.window)
                ++docked;
            QMenu menu;
            QAction *rename = menu.addAction(tr("Rename…"));
            QAction *detach = menu.addAction(tr("Open in own window"));
            // Keep at least one account in the main window.
            detach->setEnabled(docked > 1);
            menu.addSeparator();
            QAction *remove = menu.addAction(tr("Remove account"));
            // The default account is the app's own session; renamable but not
            // removable, or there would be nothing to fall back to.
            remove->setEnabled(!m_accounts[index].id.isEmpty() &&
                               m_accounts.size() > 1);
            QAction *chosen = menu.exec(m_accountBar->mapToGlobal(pos));
            if (chosen == rename)
              renameAccount(index);
            else if (chosen == detach)
              detachAccount(index);
            else if (chosen == remove)
              removeAccount(index);
          });

  // A tab drag ended -> route by what it landed on (strip already handled it,
  // another window's body, or empty space).
  connect(m_accountBar, &AccountTabBar::dragReleased, this,
          [this](const QString &id, const QPoint &globalPos) {
            // Defer: we are inside the tab bar's event handling; let it unwind
            // before rebuilding the tabs.
            QTimer::singleShot(0, this, [this, id, globalPos]() {
              onTabDragReleased(id, globalPos);
            });
          });

  // Drop a tab onto the main strip -> dock that account here at the slot
  // (bring a detached account back in). Within-strip reordering is handled by
  // tabMoved below, since QTabBar slides those tabs itself.
  connect(m_accountBar, &AccountTabBar::accountDropped, this,
          [this](const QString &id, int insertSlot) {
            m_tabDropHandledByStrip = true; // consumed here; skip geometry routing
            QTimer::singleShot(0, this, [this, id, insertSlot]() {
              dockAccountToMainAt(id, insertSlot);
            });
          });

  // The user slid a tab within the strip: keep the "+" affordance last and
  // re-derive the docked account order from the new tab order.
  connect(m_accountBar, &QTabBar::tabMoved, this, [this](int, int) {
    if (m_reorderingTabs)
      return;
    int plusTab = -1;
    for (int t = 0; t < m_accountBar->count(); ++t)
      if (!m_accountBar->tabData(t).isValid()) {
        plusTab = t;
        break;
      }
    if (plusTab >= 0 && plusTab != m_accountBar->count() - 1) {
      m_reorderingTabs = true;
      m_accountBar->moveTab(plusTab, m_accountBar->count() - 1);
      m_reorderingTabs = false;
    }
    reorderWindowFromStrip(nullptr);
  });
}

void MainWindow::showCommandPalette() {
  QList<CommandPalette::Command> cmds;

  // Every menu/keyboard action, by its (cleaned) text.
  const QList<QAction *> actions = {
      m_reloadAction,      m_minimizeAction,  m_restoreAction,
      m_lockAction,        m_muteAction,      m_fullscreenAction,
      m_openUrlAction,     m_scheduledMessagesAction, m_toggleThemeAction,
      m_chatListStripAction, m_settingsAction, m_aboutAction,
      m_translateSelectionAction, m_translateComposerAction,
      m_exportChatAction, m_aiSummarizeAction, m_aiImproveAction,
      m_aiSuggestAction,   m_aiUnreadDigestAction,
      m_aiFormalAction,    m_aiFriendlyAction, m_aiShorterAction,
      m_dndAction,         m_dnd1hAction,     m_dnd2hAction,
      m_dndMorningAction,
      m_remind1hAction,    m_remind3hAction,  m_remindTomorrowAction,
      m_viewTabsAction,    m_viewGridAction,  m_spellNextAction,
      m_quitAction};
  for (QAction *a : actions) {
    if (!a)
      continue;
    QString label = a->text();
    label.remove(QLatin1Char('&')); // strip mnemonics
    cmds.append({label, [a]() { a->trigger(); }});
  }

  // Switch to each account.
  for (int i = 0; i < m_accounts.size(); ++i) {
    const QString name = m_accounts[i].name;
    cmds.append({tr("Switch to account: %1").arg(name),
                 [this, i]() { setActiveAccount(i); }});
  }
  cmds.append({tr("Add account…"), [this]() { promptAddAccount(); }});
  cmds.append({tr("Quick message…"), [this]() { showQuickCompose(); }});

  // Saved replies: insert the text straight into the message box.
  for (const CannedResponses::Response &r : CannedResponses::all()) {
    const QString text = r.text;
    cmds.append({tr("Insert: %1").arg(r.title), [this, text]() {
                   if (m_webEngine && m_webEngine->page())
                     m_webEngine->page()->runJavaScript(
                         CannedResponses::insertScript(text));
                 }});
  }

  // Parent to the focused window so the palette opens over whichever window the
  // user is in (main or a detached one), not always the main window.
  QWidget *host = QApplication::activeWindow();
  auto *palette = new CommandPalette(cmds, host ? host : this);
  palette->setAttribute(Qt::WA_DeleteOnClose);
  // Non-modal so a click outside dismisses it (CommandPalette closes itself on
  // deactivation); it runs commands through the stored lambdas, so no exec().
  palette->show();
  palette->raise();
  palette->activateWindow();
}

// Tear the grid down, first rescuing the account views (which are owned by the
// app, not by the tiles) so deleting the splitter tree never deletes a view.
void MainWindow::clearGridCells() {
  for (const Account &account : m_accounts)
    if (account.view && m_gridContainer &&
        m_gridContainer->isAncestorOf(account.view)) {
      account.view->setParent(nullptr);
      account.view->setMinimumSize(0, 0); // drop the grid-only minimum
    }
  m_gridRowSplits.clear();
  if (m_gridVSplit) {
    delete m_gridVSplit; // deletes the row splitters + tile wrappers + captions
    m_gridVSplit = nullptr;
  }
  if (m_gridContainer)
    m_gridContainer->setMinimumSize(0, 0);
  m_gridMinSize = QSize(); // stop syncGridContainerSize sizing a torn-down grid
  m_gridLabels.clear();
}

// The minimum size of one grid tile. Like the window minimum
// (applyMinimumSize), it follows a zoomed-out page down, so zooming out fits
// more accounts on screen instead of pinning every tile at full size.
QSize MainWindow::gridTileMinSize() const {
  const double zoom = SettingsManager::instance()
                          .settings()
                          .value("zoomFactor", 1.0)
                          .toDouble();
  const double factor = qBound(0.5, zoom, 1.0);
  return QSize(static_cast<int>(kGridMinWidth * factor),
               static_cast<int>(kGridMinHeight * factor));
}

// Build the grid as a vertical splitter of rows, each a horizontal splitter of
// tiles (caption + account view). Dragging a divider resizes a row or column;
// column widths are mirrored across rows so a column stays uniform. Every view
// keeps the WebApp's usable minimum, so the scroll area scrolls rather than
// hiding a tile when the window is too small.
void MainWindow::relayoutGrid() {
  if (!m_gridContainer)
    return;
  clearGridCells();

  const int n = m_accounts.size();
  if (n == 0)
    return;

  const int cols = qMax(1, static_cast<int>(qCeil(qSqrt(qreal(n)))));
  const int rows = (n + cols - 1) / cols;
  const QSize tileMin = gridTileMinSize();

  m_gridVSplit = new QSplitter(Qt::Vertical, m_gridContainer);
  m_gridVSplit->setChildrenCollapsible(false);
  m_gridVSplit->setHandleWidth(kGridHandle);
  m_gridContainer->layout()->addWidget(m_gridVSplit);

  int idx = 0;
  for (int r = 0; r < rows; ++r) {
    auto *rowSplit = new QSplitter(Qt::Horizontal, m_gridVSplit);
    rowSplit->setChildrenCollapsible(false);
    rowSplit->setHandleWidth(kGridHandle);
    for (int c = 0; c < cols && idx < n; ++c, ++idx) {
      WebView *view = m_accounts[idx].view;
      if (!view) {
        m_gridLabels.append(QPointer<QLabel>(nullptr));
        continue;
      }
      // Each tile is a caption (account name + unread) above its account view,
      // so it is obvious which tile is which.
      auto *cell = new QWidget;
      auto *box = new QVBoxLayout(cell);
      box->setContentsMargins(0, 0, 0, 0);
      box->setSpacing(0);
      auto *caption = new QLabel(cell);
      caption->setObjectName(QStringLiteral("gridCellCaption"));
      caption->setAlignment(Qt::AlignCenter);
      caption->setContentsMargins(4, 2, 4, 2);
      view->setAccessibleName(m_accounts[idx].name);
      view->setMinimumSize(tileMin);
      box->addWidget(caption);
      box->addWidget(view, 1); // reparents the view into the tile, from anywhere
      m_gridLabels.append(caption);
      rowSplit->addWidget(cell);
      view->show();
    }
    m_gridVSplit->addWidget(rowSplit);
    m_gridRowSplits.append(rowSplit);
    // Dragging a column divider in one row mirrors to the others and counts as
    // a user customization.
    connect(rowSplit, &QSplitter::splitterMoved, this,
            [this, rowSplit](int, int) {
              if (m_gridSyncing)
                return;
              syncGridColumns(rowSplit);
              markGridCustomized();
            });
  }
  // Dragging a row divider counts as a user customization.
  connect(m_gridVSplit, &QSplitter::splitterMoved, this, [this](int, int) {
    if (m_gridSyncing)
      return;
    markGridCustomized();
  });

  // The whole grid at minimum tile size. syncGridContainerSize keeps the
  // container at least this big, so the scroll area shows a single full-grid
  // scrollbar when the window is too small — instead of shrinking each tile
  // until it grows its own scrollbar.
  const int captionH = 24;
  m_gridMinSize =
      QSize(cols * tileMin.width() + (cols - 1) * kGridHandle,
            rows * (tileMin.height() + captionH) + (rows - 1) * kGridHandle);
  syncGridContainerSize();
  updateGridCaptions();
}

// Keep the grid container at max(viewport, whole-grid-minimum) so the scroll
// area (not the tiles) owns the scrollbars.
void MainWindow::syncGridContainerSize() {
  if (!m_gridContainer || !m_gridScroll || m_gridMinSize.isEmpty())
    return;
  const QSize vp = m_gridScroll->viewport()->size();
  m_gridContainer->resize(qMax(vp.width(), m_gridMinSize.width()),
                          qMax(vp.height(), m_gridMinSize.height()));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (m_gridScroll && watched == m_gridScroll->viewport() &&
      event->type() == QEvent::Resize)
    syncGridContainerSize();
  // About to be drawn is the real trigger for building a dormant account, and it
  // is the only one that catches every case. Switching tabs is not enough: a
  // detached window shows its own account without touching setActiveAccount(),
  // and grid mode draws every account at once, so both would otherwise paint a
  // blank view. Hooking the view's own visibility covers all of them, including
  // any future path that puts an account on screen.
  if (event->type() == QEvent::Show) {
    const int idx = accountIndexForView(watched);
    if (idx >= 0) {
      ensureAccountLoaded(idx);
      // Then rebuild the surface, whether or not the call above had anything to
      // do. Moving a QWebEngineView between top-level windows can leave its
      // native surface behind, and nothing else in the app is watching for that,
      // so an account torn into a new window came up black and stayed black
      // until it was docked and torn out again.
      //
      // It used to run only for an account that was already loaded, on the
      // reasoning that a page built right here would come up fresh anyway. A
      // second account then came up black too, and the window was pure black to
      // the pixel with "Compositor returned null texture" logged as it opened:
      // the view had no picture at all, which is a fact about the surface and
      // not about the page. Nothing in that says the load state should decide.
      //
      // The nudge is a hide/show on the next turn of the event loop, which makes
      // Qt build the surface again. It is one-shot per view: it causes another
      // Show, which lands back here, and without the guard that is an endless
      // loop — the same shape as the tint recursion.
      nudgeReparentedView(idx);
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

// Mirror one row's column widths onto every other row, so a column resize is
// uniform across the grid.
void MainWindow::syncGridColumns(QSplitter *source) {
  if (!source)
    return;
  const QList<int> sizes = source->sizes();
  m_gridSyncing = true;
  for (QSplitter *rs : m_gridRowSplits)
    if (rs && rs != source && rs->count() == sizes.size())
      rs->setSizes(sizes);
  m_gridSyncing = false;
}

// Snapshot the current dividers and grid window size, so a customized layout can
// be reapplied on re-entry and (when opted in) persisted across restarts.
void MainWindow::captureGridSizes() {
  if (m_gridVSplit)
    m_gridSavedRows = m_gridVSplit->sizes();
  if (!m_gridRowSplits.isEmpty() && m_gridRowSplits.first())
    m_gridSavedCols = m_gridRowSplits.first()->sizes();
  if (!isMaximized() && !isFullScreen())
    m_gridSavedGeom = geometry();
}

// The user dragged a divider (or resized the grid window): remember it.
void MainWindow::markGridCustomized() {
  if (m_gridResizing || m_viewMode != ViewMode::Grid)
    return;
  m_gridCustomized = true;
  captureGridSizes();
  saveWindowLayout();
}

// Grow the window so a grid of every account fits at the WebApp's usable
// minimum, capped to the screen (the scroll area covers any shortfall).
void MainWindow::growWindowForGrid() {
  if (isMaximized() || isFullScreen())
    return; // already plenty of room, or cannot resize — the scroll area copes
  const int n = qMax(1, m_accounts.size());
  const int cols = qMax(1, static_cast<int>(qCeil(qSqrt(qreal(n)))));
  const int rows = (n + cols - 1) / cols;
  const int captionH = 24;
  const QSize tileMin = gridTileMinSize();
  // Ask for the whole grid at minimum PLUS some slack. Growing to exactly the
  // minimum leaves every tile pinned at its floor, so a divider cannot give one
  // tile room without taking it from a neighbour that has none to give — the
  // dividers look broken until the window is enlarged by hand.
  const int needW =
      cols * tileMin.width() + (cols - 1) * kGridHandle + kGridSlack;
  const int needH = rows * (tileMin.height() + captionH) +
                    (rows - 1) * kGridHandle + kGridSlack;
  // Grow the WINDOW by however much more the display area needs, so window
  // chrome (title bar, margins) is accounted for automatically.
  const QSize cur = m_displayStack ? m_displayStack->size() : size();
  int targetW = width() + qMax(0, needW - cur.width());
  int targetH = height() + qMax(0, needH - cur.height());
  int nx = x(), ny = y();
  if (QScreen *scr = screen()) {
    const QRect avail = scr->availableGeometry();
    targetW = qMin(targetW, avail.width());
    targetH = qMin(targetH, avail.height());
    // Growing can push the window past the screen edge — slide it back so the
    // whole (now larger) window is visible.
    if (nx + targetW > avail.right())
      nx = avail.right() - targetW;
    if (ny + targetH > avail.bottom())
      ny = avail.bottom() - targetH;
    nx = qMax(nx, avail.left());
    ny = qMax(ny, avail.top());
  }
  m_gridResizing = true;
  resize(targetW, targetH);
  move(nx, ny);
  QTimer::singleShot(0, this, [this]() { m_gridResizing = false; });
}

// Distribute the rows and columns equally and grow the window to fit; clears the
// "customized" flag so nothing is remembered until the user drags again.
void MainWindow::resetGridTiles() {
  if (m_viewMode != ViewMode::Grid)
    return;
  m_gridCustomized = false;
  growWindowForGrid();
  m_gridSyncing = true;
  if (m_gridVSplit)
    m_gridVSplit->setSizes(QList<int>(m_gridVSplit->count(), 1 << 16));
  for (QSplitter *rs : m_gridRowSplits)
    if (rs)
      rs->setSizes(QList<int>(rs->count(), 1 << 16));
  m_gridSyncing = false;
  saveWindowLayout(); // persists gridCustomized = false
}

// Reapply a remembered custom layout (dividers + window size). If the saved
// shape no longer matches the current tile count, fall back to a clean reset.
void MainWindow::applyGridSizes() {
  const int rows = m_gridVSplit ? m_gridVSplit->count() : 0;
  const int cols = (!m_gridRowSplits.isEmpty() && m_gridRowSplits.first())
                       ? m_gridRowSplits.first()->count()
                       : 0;
  if (rows == 0 || m_gridSavedRows.size() != rows ||
      m_gridSavedCols.size() != cols) {
    resetGridTiles();
    return;
  }
  if (m_gridSavedGeom.isValid() && !isMaximized() && !isFullScreen()) {
    m_gridResizing = true;
    setGeometry(m_gridSavedGeom);
    QTimer::singleShot(0, this, [this]() { m_gridResizing = false; });
  }
  m_gridSyncing = true;
  m_gridVSplit->setSizes(m_gridSavedRows);
  for (QSplitter *rs : m_gridRowSplits)
    if (rs)
      rs->setSizes(m_gridSavedCols);
  m_gridSyncing = false;
}

// Keep each tile's caption in step with the account name and unread count.
void MainWindow::updateGridCaptions() {
  for (int i = 0; i < m_gridLabels.size() && i < m_accounts.size(); ++i) {
    QLabel *label = m_gridLabels.at(i);
    if (!label)
      continue;
    const Account &account = m_accounts[i];
    label->setText(account.unread > 0
                       ? tr("%1 — %2 unread").arg(account.name).arg(account.unread)
                       : account.name);
  }
}

void MainWindow::setViewMode(ViewMode mode) {
  m_viewMode = mode;
  SettingsManager::instance().settings().setValue(
      "viewMode", static_cast<int>(mode));

  QSet<DetachedAccountWindow *> wins;
  for (const Account &a : m_accounts)
    if (a.window)
      wins.insert(a.window);

  if (mode == ViewMode::Grid) {
    // Remember the tabbed size to restore when Grid is left.
    if (!isMaximized() && !isFullScreen())
      m_preGridGeometry = geometry();
    // Collapse everything: hide the strip and the detached windows, and pull
    // every account's view into the tiles.
    m_accountBar->hide();
    for (DetachedAccountWindow *w : wins)
      w->hide();
    relayoutGrid(); // build the splitter tree, reparenting views into tiles
    m_displayStack->setCurrentWidget(m_gridScroll);
    // Reapply the user's remembered layout, or lay the tiles out equally and
    // grow the window so each is at least the WebApp's usable minimum.
    if (m_gridCustomized)
      applyGridSizes();
    else
      resetGridTiles();
  } else {
    // Restore: rescue the views out of the tiles, hand each back to the window
    // it belongs to, then show the strips and the detached windows again.
    clearGridCells();
    for (int i = 0; i < m_accounts.size(); ++i) {
      WebView *view = m_accounts[i].view;
      if (!view)
        continue;
      if (m_accounts[i].window)
        m_accounts[i].window->stack()->addWidget(view);
      else
        m_accountStack->addWidget(view);
    }
    for (DetachedAccountWindow *w : wins)
      w->show();
    m_displayStack->setCurrentWidget(m_accountStack);
    refreshAccountTabs(); // rebuild main + detached strips; sets strip visibility
    setActiveAccount(m_activeAccount);
    // Return to the size the window had before Grid grew it.
    if (m_preGridGeometry.isValid()) {
      setGeometry(m_preGridGeometry);
      m_preGridGeometry = QRect();
    }
  }

  if (m_viewTabsAction)
    m_viewTabsAction->setChecked(mode == ViewMode::Tabs);
  if (m_viewGridAction)
    m_viewGridAction->setChecked(mode == ViewMode::Grid);
}

WebView *MainWindow::addAccount(const QString &id, const QString &name,
                                bool load) {
  auto *view = new WebView(m_accountStack);
  view->accountId = id;
  view->addAction(m_minimizeAction);
  view->addAction(m_lockAction);
  view->addAction(m_quitAction);

  // Right-click menu: the discoverable path to Whatly's text actions, so they
  // need no shortcut or command palette. Composer actions appear in the message
  // box, selection actions when text is selected, chat actions always.
  view->setContextActions(
      {m_aiImproveAction, m_aiFormalAction, m_aiFriendlyAction,
       m_aiShorterAction, m_aiSuggestAction, m_translateComposerAction},
      {m_translateSelectionAction},
      {m_aiSummarizeAction, m_aiUnreadDigestAction, m_remind1hAction,
       m_remind3hAction, m_remindTomorrowAction, m_exportChatAction});

  // Watched so that whatever eventually puts this view on screen — a tab switch,
  // grid mode, a detached window — builds the account first. See eventFilter().
  view->installEventFilter(this);
  m_accountStack->addWidget(view);
  m_accounts.append({id, name, view, 0});
  m_accounts.last().lastActive = QDateTime::currentDateTime();

  // The active view is the one the rest of MainWindow drives through
  // m_webEngine; without a page yet, point it here so the first account is
  // usable before its page finishes loading.
  if (!m_webEngine)
    m_webEngine = view;

  if (load)
    createPageFor(view, id);

  // In grid mode the new account joins the tiles right away.
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  return view;
}

void MainWindow::setActiveAccount(int index) {
  if (index < 0 || index >= m_accounts.size())
    return;
  if (m_accounts[index].window)
    return; // detached: it lives in its own window, not the main strip/stack
  // The account we are leaving starts its idle clock now.
  if (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
    m_accounts[m_activeAccount].lastActive = QDateTime::currentDateTime();
  m_activeAccount = index;
  m_webEngine = m_accounts[index].view;   // everything current-account flows through this
  m_accounts[index].lastActive = QDateTime::currentDateTime();
  // Opening a dormant account is when it comes into existence.
  ensureAccountLoaded(index);
  m_accountStack->setCurrentWidget(m_accounts[index].view);
  // Point the strip at the tab carrying this account's id.
  QSignalBlocker block(m_accountBar);
  const QString id = m_accounts[index].id;
  for (int t = 0; t < m_accountBar->count(); ++t) {
    const QVariant d = m_accountBar->tabData(t);
    if (d.isValid() && d.toString() == id) {
      m_accountBar->setCurrentIndex(t);
      break;
    }
  }
  // Remember the account, so the next start reopens here instead of always
  // falling back to the first tab. Guarded exactly like saveAccounts(): a layout
  // restore or the quit-time collapse of detached windows both drive this
  // function, and neither is the user choosing an account. Stored as a token
  // when it is the default account, whose real id is the empty string.
  if (!m_loadingLayout && !m_isQuitting) {
    const QString kDefault = QStringLiteral("__default__");
    SettingsManager::instance().settings().setValue(
        QStringLiteral("accounts/active"), id.isEmpty() ? kDefault : id);
  }
  // Re-point the lock overlay and refresh the title to the now-active account.
  setWindowTitle(accountTitle(m_activeAccount));
}

QString MainWindow::accountTitle(int idx) const {
  if (idx < 0 || idx >= m_accounts.size())
    return AppProfile::label().trimmed();
  const Account &a = m_accounts[idx];
  QString title = a.name;
  if (a.unread > 0)
    title += QStringLiteral(" (%1)").arg(a.unread);
  return title + AppProfile::label();
}

QWidget *MainWindow::windowShowingAccount(int idx) const {
  if (idx < 0 || idx >= m_accounts.size())
    return nullptr;
  const Account &a = m_accounts[idx];
  if (!a.window)
    return idx == m_activeAccount ? const_cast<MainWindow *>(this) : nullptr;
  AccountTabBar *bar = a.window->bar();
  if (!bar)
    return nullptr;
  // Validity first, as everywhere the strip is asked about an account: the "+"
  // affordance carries no tab data and the default account's id is the empty
  // string, so comparing ids alone would answer yes for the affordance.
  const QVariant data = bar->tabData(bar->currentIndex());
  return data.isValid() && data.toString() == a.id
             ? static_cast<QWidget *>(a.window.data())
             : nullptr;
}

int MainWindow::accountIndexForView(const QObject *view) const {
  for (int i = 0; i < m_accounts.size(); ++i)
    if (m_accounts[i].view == view)
      return i;
  return -1;
}

int MainWindow::accountIndexForId(const QString &id) const {
  for (int i = 0; i < m_accounts.size(); ++i)
    if (m_accounts[i].id == id)
      return i;
  return -1;
}

int MainWindow::focusedAccountIndex() const {
  // A detached window shows whatever its own stack is on. Its tab strip swaps
  // that stack directly and never sets m_activeAccount, so for any window other
  // than this one the app-wide "active" account is simply the wrong answer.
  if (auto *win =
          qobject_cast<DetachedAccountWindow *>(QApplication::activeWindow())) {
    const QWidget *shown = win->stack()->currentWidget();
    for (int i = 0; i < m_accounts.size(); ++i)
      if (m_accounts[i].view == shown)
        return i;
  }
  // The main window, or a key that arrived with no active window at all (a tray
  // menu item, say) — then the account this window is on is the best answer.
  if (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
    return m_activeAccount;
  return -1;
}

// A tab was dropped onto the main strip. Move that account into the main window
// at slot `insertSlot` among the docked tabs — either reordering a tab already
// here, or bringing a detached account back in. The just-dropped tab takes
// focus, since the drop is a deliberate placement.
void MainWindow::dockAccountToMainAt(const QString &id, int insertSlot) {
  const int idx0 = accountIndexForId(id);
  if (idx0 < 0)
    return;
  const bool wasDetached = (m_accounts[idx0].window != nullptr);

  // The account's current slot among docked tabs (only meaningful for a reorder
  // of a tab already on the main strip).
  int origSlot = -1;
  for (int i = 0, s = 0; i < m_accounts.size(); ++i) {
    if (m_accounts[i].window)
      continue;
    if (i == idx0) {
      origSlot = s;
      break;
    }
    ++s;
  }

  const QString activeId =
      (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
          ? m_accounts[m_activeAccount].id
          : QString();

  // Bring a detached account's view back into the main stack; close its source
  // window only if that leaves it empty (a window may hold several accounts).
  if (wasDetached) {
    DetachedAccountWindow *win = m_accounts[idx0].window;
    m_accounts[idx0].window = nullptr;
    if (m_accounts[idx0].view)
      m_accountStack->addWidget(m_accounts[idx0].view); // reparents back
    if (win) {
      bool empty = true;
      for (const Account &a : m_accounts)
        if (a.window == win) {
          empty = false;
          break;
        }
      if (empty)
        destroyDetachedWindow(win);
    }
  }

  // Removing the account shifts later docked slots left by one (reorder only).
  if (!wasDetached && origSlot >= 0 && insertSlot > origSlot)
    --insertSlot;

  Account acc = m_accounts.takeAt(idx0);
  QList<int> docked;
  for (int i = 0; i < m_accounts.size(); ++i)
    if (!m_accounts[i].window)
      docked.append(i);
  int target;
  if (insertSlot <= 0)
    target = docked.isEmpty() ? m_accounts.size() : docked.first();
  else if (insertSlot >= docked.size())
    target = docked.isEmpty() ? m_accounts.size() : docked.last() + 1;
  else
    target = docked[insertSlot];
  m_accounts.insert(target, acc);

  // Keep the previously-active account active, then focus the dropped tab.
  m_activeAccount = accountIndexForId(activeId);
  if (m_activeAccount < 0)
    m_activeAccount = 0;
  refreshAccountTabs();
  setActiveAccount(accountIndexForId(id));
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  updateTrayUnread();
  saveAccounts();
}

// After the user slides a tab, that window's strip is the source of truth.
// Re-derive the order of its accounts in m_accounts to match, leaving accounts
// in other windows in place. The strip already shows the new order, so no
// refresh. win == nullptr means the main window.
void MainWindow::reorderWindowFromStrip(DetachedAccountWindow *win) {
  AccountTabBar *bar = win ? win->bar() : m_accountBar;
  if (!bar)
    return;
  QStringList order; // account ids in this strip's current tab order
  for (int t = 0; t < bar->count(); ++t) {
    const QVariant d = bar->tabData(t);
    if (d.isValid())
      order << d.toString();
  }
  const QString activeId =
      (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
          ? m_accounts[m_activeAccount].id
          : QString();

  QList<Account> orderedMembers;
  for (const QString &id : order) {
    const int idx = accountIndexForId(id);
    if (idx >= 0 && m_accounts[idx].window == win)
      orderedMembers.append(m_accounts[idx]);
  }

  // Rebuild: accounts in other windows keep their positions; this window's
  // slots are filled in the new order.
  QList<Account> rebuilt;
  int di = 0;
  for (const Account &a : m_accounts) {
    if (a.window == win) {
      if (di < orderedMembers.size())
        rebuilt.append(orderedMembers[di++]);
    } else {
      rebuilt.append(a);
    }
  }
  if (rebuilt.size() != m_accounts.size())
    return; // counts disagree — leave things untouched rather than corrupt them

  m_accounts = rebuilt;
  m_activeAccount = accountIndexForId(activeId);
  if (m_activeAccount < 0)
    m_activeAccount = 0;
  saveAccounts();
}

void MainWindow::captureAccountVersion(WebView *view) {
  const int idx = view ? accountIndexForView(view) : -1;
  QWebEnginePage *page = idx >= 0 ? pageOf(m_accounts[idx]) : nullptr;
  if (!page)
    return;
  QPointer<WebView> guarded(view);
  page->runJavaScript(
      QStringLiteral("(window.Debug && window.Debug.VERSION) || ''"),
      [this, guarded](const QVariant &result) {
        if (!guarded)
          return;
        const QString ver = result.toString();
        if (ver.isEmpty())
          return;
        const int idx = accountIndexForView(guarded);
        if (idx < 0)
          return;
        // Written to the log on every load, not only when it changes. WhatsApp Web
        // updates itself under the app without saying so, and which build an
        // account was running is the first thing worth knowing about a page that
        // misbehaved — afterwards, the log is the only place that can still say it.
        const QString was = m_accounts[idx].waVersion;
        // Named as the tab names it, so a line about one account out of four is
        // recognisable; by position when it has no name of its own.
        const QString who = m_accounts[idx].name.isEmpty()
                                ? QStringLiteral("account %1").arg(idx + 1)
                                : m_accounts[idx].name;
        qInfo().noquote()
            << QStringLiteral("whatly: %1 is on WhatsApp Web %2%3")
                   .arg(who, ver,
                        was.isEmpty() || was == ver
                            ? QString()
                            : QStringLiteral(" — was %1").arg(was));
        if (was == ver)
          return; // unchanged: nothing to relabel
        m_accounts[idx].waVersion = ver;
        refreshAccountTabs(); // also refreshes every detached window's strip
      });
}

QWebEnginePage *MainWindow::pageOf(const Account &a) {
  // Deliberately does not fall back to a.view->page(): QWebEngineView builds a
  // page on demand when asked for one it does not have, so a single stray call
  // on a dormant account would silently create a renderer bound to the default
  // profile — the opposite of what dormancy is for, and a session mix-up.
  return a.loaded && a.view ? a.view->page() : nullptr;
}

void MainWindow::nudgeReparentedView(int index) {
#ifdef Q_OS_WIN
  // Windows rebuilds the surface by itself when a view is reparented, so the
  // hide/show below has nothing to repair — it throws away a picture that was
  // already good, and the view then stays black until WhatsApp happens to repaint
  // of its own accord, which is tens of seconds on a quiet chat.
  //
  // Measured rather than reasoned, one binary run both ways with nothing else
  // differing: nudging, an account docked back out of a detached window was black
  // for 35 seconds and one entering grid view for 12; skipping it, neither
  // blacked at all, in the same four paths. Linux is the other way round — there
  // a live view moved between top-levels comes up black and stays black without
  // this — so it is a platform difference, not dead code.
  Q_UNUSED(index);
#else
  if (index < 0 || index >= m_accounts.size())
    return;
  WebView *view = m_accounts[index].view;
  if (!view || m_nudgedViews.contains(view))
    return;
  m_nudgedViews.insert(view);
  // Next turn of the event loop, not now: the window this view has just been put
  // into is still being assembled, and hiding a widget in the middle of its own
  // show event is not something Qt owes us anything for.
  QWidget *raw = view;
  QPointer<WebView> guard(view);
  QTimer::singleShot(0, this, [this, guard, raw]() {
    // Only while it is really on screen — a hide/show on a view that has since
    // been put away would be a flicker for nothing.
    if (guard && guard->isVisible()) {
      guard->hide();
      guard->show();
    }
    // Released only after the show above, whose own Show event comes back
    // through eventFilter() and must find this view still marked.
    m_nudgedViews.remove(raw);
  });
#endif
}

void MainWindow::ensureAccountLoaded(int index) {
  if (index < 0 || index >= m_accounts.size())
    return;
  Account &a = m_accounts[index];
  if (a.loaded || !a.view)
    return;
  // Marked before building, not after. createPageFor() attaches a page to the
  // view, and that can emit a show event, which comes straight back here through
  // eventFilter() — with the flag set afterwards, that would recurse forever.
  a.loaded = true;
  if (a.unloadedWithWindow) {
    // The other half of the line written when it went. Which path got here does
    // not matter — on X11 the view's own show event beats the sweep to it — and
    // the pair is what makes the log readable: a page that came back is not the
    // same event as an account being opened for the first time.
    a.unloadedWithWindow = false;
    qInfo() << "whatly: loading" << a.name << "again — its window is back";
  }
  createPageFor(a.view, a.id);
}

void MainWindow::unloadAccount(int index) {
  if (index < 0 || index >= m_accounts.size())
    return;
  Account &a = m_accounts[index];
  if (!a.loaded || !a.view)
    return;
  QWebEnginePage *page = a.view->page();
  // Cleared before the page goes, so anything reached during teardown sees a
  // dormant account and cannot ask for the page being destroyed.
  a.loaded = false;
  a.ready = false; // nothing loaded on a page that no longer exists
  // Detach first, then delete: the view holds a plain pointer to its page, and
  // deleting it without detaching would leave that pointer dangling for the next
  // caller. Detaching also drops the renderer, which is the memory we are after —
  // freezing the page would have kept every byte of it.
  a.view->setPage(nullptr);
  delete page;
}

void MainWindow::suspendIdleAccounts() {
  const bool enabled = Performance::suspendInactiveAccounts();
  const int threshold = Performance::suspendAfterMinutes() * 60;
  const QDateTime now = QDateTime::currentDateTime();
  for (int i = 0; i < m_accounts.size(); ++i) {
    Account &a = m_accounts[i];
    if (!a.view || !a.loaded)
      continue; // already dormant, nothing left to give back
    const bool isActive = (i == m_activeAccount);
    const bool isVisible = a.view->isVisible(); // grid tiles / detached windows
    const int idle = a.lastActive.isValid()
                         ? int(a.lastActive.secsTo(now))
                         : 0;
    if (Performance::shouldSuspendAccount(enabled, isActive, isVisible, idle,
                                          threshold))
      unloadAccount(i);
  }
  // The same sweep is the safety net for the window rule below: the events catch
  // a window going away while the app is running, and this catches the rest — a
  // window already minimised when the option was switched on, or one the
  // compositor put away without telling us.
  unloadOffscreenWindowAccounts();
}

bool MainWindow::windowIsOffscreen(const QWidget *w) {
  if (!w)
    return false; // an account with no window of its own is nobody's to unload
  return !w->isVisible() || w->isMinimized();
}

QWidget *MainWindow::windowHostingAccount(int idx) const {
  if (idx < 0 || idx >= m_accounts.size())
    return nullptr;
  const Account &a = m_accounts[idx];
  return a.window ? static_cast<QWidget *>(a.window.data())
                  : const_cast<MainWindow *>(this);
}

bool MainWindow::refreshOffscreenSince() {
  const QDateTime now = QDateTime::currentDateTime();
  QHash<const QWidget *, QDateTime> stamps;
  bool wentAway = false;
  for (const QWidget *w : allWindows()) {
    if (!windowIsOffscreen(w))
      continue; // on screen: it has no absence to time
    const QDateTime since = m_offscreenSince.value(w);
    stamps.insert(w, since.isValid() ? since : now);
    if (!since.isValid())
      wentAway = true;
  }
  // Built fresh rather than edited, so a window that came back and one that was
  // closed both drop out without being looked for.
  m_offscreenSince = stamps;
  return wentAway;
}

void MainWindow::unloadOffscreenWindowAccounts() {
  const bool enabled = Performance::suspendInactiveAccounts();
  const bool alsoOffscreen = Performance::unloadOffscreenWindows();
  if (!enabled || !alsoOffscreen) {
    // Rule off: drop the timing state, so re-enabling it later starts the wait
    // from now rather than from a stamp left over from when it was last on —
    // which would unload a still-hidden window's account the instant it is
    // switched back on, against the "wait starts now" contract above. The 60s
    // sweep calls this whatever the settings say, so the clear always lands.
    m_offscreenSince.clear();
    return; // the common case, and the loop below has nothing to say about it
  }
  // Stamped here as well as on the events, so switching the option on while the
  // window is already away starts the wait from now rather than unloading on the
  // spot — and so the sweep can be the whole story on a platform that reports no
  // window state change at all.
  refreshOffscreenSince();
  const int threshold = Performance::suspendAfterMinutes() * 60;
  const QDateTime now = QDateTime::currentDateTime();
  for (int i = 0; i < m_accounts.size(); ++i) {
    if (!m_accounts[i].view || !m_accounts[i].loaded)
      continue; // already dormant
    const QWidget *host = windowHostingAccount(i);
    const QDateTime since = m_offscreenSince.value(host);
    const int away = since.isValid() ? int(since.secsTo(now)) : 0;
    if (Performance::shouldUnloadWithWindow(enabled, alsoOffscreen,
                                            windowIsOffscreen(host), away,
                                            threshold)) {
      // Logged, because from the outside this looks like the account having been
      // thrown away: come back to the window and WhatsApp Web is loading again,
      // with no message and nothing in the interface to say why. One line per
      // account, and only when something really was unloaded.
      qInfo() << "whatly: unloading" << m_accounts[i].name
              << "with the window it was in";
      unloadAccount(i);
      m_accounts[i].unloadedWithWindow = true; // after: unloading clears nothing
    }
  }
}

void MainWindow::reloadOnscreenAccounts() {
  for (int i = 0; i < m_accounts.size(); ++i) {
    const Account &a = m_accounts[i];
    if (!a.view || a.loaded)
      continue;
    // Only what is actually drawn. The other accounts of a window are dormant on
    // purpose — that is the whole feature — and the view's own visibility is what
    // tells them apart, in tab mode and in the grid alike. The window is asked as
    // well because the view's answer is not the same everywhere: minimising on
    // X11 hides the view with the window, and where it does not, the view alone
    // would say a minimised window's account should be built again.
    if (a.view->isVisible() && !windowIsOffscreen(windowHostingAccount(i)))
      ensureAccountLoaded(i); // which does the saying, for either path
  }
}

void MainWindow::noteWindowVisibilityChanged() {
  // Unconditional, and first: a page that was thrown away has to come back
  // however the setting has moved since it went, or turning the option off would
  // leave the window it was on blank until the next tab switch. Deferred by one
  // turn of the event loop because a window coming back is not finished doing so
  // when the event arrives — on a restore from minimised the state is already
  // new, but on a show the children are not visible yet.
  QTimer::singleShot(0, this, [this]() { reloadOnscreenAccounts(); });
  if (!Performance::suspendInactiveAccounts() ||
      !Performance::unloadOffscreenWindows())
    return; // nothing to time
  refreshOffscreenSince();
  armOffscreenTimer();
}

// The single timer, set for the first deadline still to come rather than for a
// fresh full delay (issue #91). There is one timer and a wait per window, so
// arming it for the delay every time any window goes hands it to whichever went
// last: window A hides, then window B hides a minute later, and B's arming moves
// the timer a minute past A's deadline. Reading the earliest of the pending
// waits instead means the timer always belongs to the window that needs it
// soonest, and re-arming after each timeout carries it on to the next one.
//
// Deadlines already passed are left out on purpose. Their windows have just been
// through unloadOffscreenWindowAccounts(), and one that still has a loaded
// account after that is the minute-long sweep's to collect, exactly as before
// — arming for a wait that is already up would fire, find the same window still
// listed, and arm for it again.
void MainWindow::armOffscreenTimer() {
  if (!m_offscreenTimer)
    return;
  // A second past the wait, so a check cannot arrive in the same second the
  // window went and find the wait a hair short.
  const qint64 wait = qint64(Performance::suspendAfterMinutes()) * 60000 + 1000;
  const QDateTime now = QDateTime::currentDateTime();
  qint64 soonest = -1;
  for (auto it = m_offscreenSince.cbegin(); it != m_offscreenSince.cend();
       ++it) {
    if (!it.value().isValid())
      continue;
    const qint64 left = wait - it.value().msecsTo(now);
    if (left <= 0)
      continue; // its wait is up; the unload and the sweep have it
    if (soonest < 0 || left < soonest)
      soonest = left;
  }
  // Nothing left to wait for: every window is back, or every wait is up. Stop
  // rather than leave the last one running: it would fire on a set of windows it
  // was not armed for.
  if (soonest < 0) {
    m_offscreenTimer->stop();
    return;
  }
  m_offscreenTimer->start(int(soonest));
}

// Every account that has a page, and the last answer is kept for the ones that
// do not. An account with no page is exactly the account you need a way back
// into: its window may be behind everything or put away, and dropping its chats
// from this menu removes the one handle that would have brought it back. The
// entries go stale, which is the honest state of a list nobody is reading — a
// week-old chat name still opens the right conversation, and opening it is the
// point.
//
// Driven by every title change, and WhatsApp changes the title whenever a message
// arrives anywhere, so the scan itself is rate-limited; the menu is rebuilt from
// what is already known every time.
void MainWindow::refreshRecentUnread() {
  if (!m_recentUnreadMenu)
    return;
  if (!m_recentUnreadScan.isValid() || m_recentUnreadScan.elapsed() > 3000) {
    m_recentUnreadScan.restart();
    for (const Account &account : m_accounts) {
      QWebEnginePage *page = pageOf(account);
      if (!page)
        continue; // dormant: keep whatever it last reported
      const QString accountId = account.id;
      page->runJavaScript(
          ChatNav::unreadChatsScript(6),
          [this, accountId](const QVariant &result) {
            const int idx = accountIndexForId(accountId);
            if (idx < 0)
              return;
            QList<QPair<QString, int>> found;
            const QJsonArray arr =
                QJsonDocument::fromJson(result.toString().toUtf8()).array();
            for (const QJsonValue &v : arr) {
              const QString name =
                  v.toObject().value(QStringLiteral("name")).toString();
              if (name.isEmpty())
                continue;
              found << qMakePair(
                  name, v.toObject().value(QStringLiteral("count")).toInt());
            }
            m_accounts[idx].recentUnread = found;
            rebuildRecentUnreadMenu();
          });
    }
  }
  rebuildRecentUnreadMenu();
}

// Entries are REUSED rather than cleared and rebuilt. Picking one switches
// account, which reaches the tray unread count, which comes back here — and
// deleting the action whose signal is still being delivered is a crash, not an
// inefficiency. So the pool only grows, spare entries are hidden, and what each
// entry points at is remembered beside it.
void MainWindow::rebuildRecentUnreadMenu() {
  if (!m_recentUnreadMenu)
    return;
  // Which account each chat belongs to is only worth saying when more than one
  // account has anything to show.
  int contributing = 0;
  for (const Account &account : m_accounts)
    if (!account.recentUnread.isEmpty())
      ++contributing;

  QList<QAction *> entries = m_recentUnreadMenu->actions();
  m_recentUnreadTargets.clear();
  QStringList labels;
  for (const Account &account : m_accounts) {
    for (const QPair<QString, int> &chat : account.recentUnread) {
      QString label = chat.first;
      if (label.size() > 32)
        label = label.left(31) + QChar(0x2026);
      label = QStringLiteral("%1  (%2)").arg(label).arg(chat.second);
      if (contributing > 1)
        label = account.name + QStringLiteral(" · ") + label;
      labels << label;
      m_recentUnreadTargets << qMakePair(account.id, chat.first);
    }
  }

  while (entries.size() < labels.size()) {
    const int slot = entries.size();
    QAction *entry = m_recentUnreadMenu->addAction(QString());
    connect(entry, &QAction::triggered, this, [this, slot]() {
      if (slot < m_recentUnreadTargets.size())
        openChatByName(m_recentUnreadTargets[slot].first,
                       m_recentUnreadTargets[slot].second);
    });
    entries << entry;
  }
  for (int i = 0; i < entries.size(); ++i) {
    entries[i]->setVisible(i < labels.size());
    if (i < labels.size())
      entries[i]->setText(labels[i]);
  }
  m_recentUnreadMenu->menuAction()->setVisible(!labels.isEmpty());
}

void MainWindow::openChatByName(const QString &accountId, const QString &name) {
  const int idx = accountIndexForId(accountId);
  if (idx < 0)
    return;
  // A tray pick is a request to use Whatly, so a window has to come up — the one
  // that actually holds this account. Raising this one and then switching a chat
  // in a window the user cannot see is worse than doing nothing.
  QWidget *host = m_accounts[idx].window
                      ? static_cast<QWidget *>(m_accounts[idx].window)
                      : static_cast<QWidget *>(this);
  // bringForward() is #55's, and it is the same four calls with the ordering
  // that survives a hidden window; using it here keeps one way of raising a
  // window in the app rather than two that can drift apart.
  bringForward(host);
  // A detached account is already the one its own window shows, so this is a
  // no-op there and a tab switch here.
  setActiveAccount(idx);
  // And it may have had no page at all until a moment ago.
  ensureAccountLoaded(idx);
  QWebEnginePage *page = pageOf(m_accounts[idx]);
  if (!page)
    return;
  // A page built for this pick has not loaded WhatsApp yet, so the chat it is
  // being asked for does not exist on it. Remember the request and run it when
  // the page reports itself loaded; only one is ever pending, because a second
  // pick replaces what the user asked for.
  if (!m_accounts[idx].ready) {
    m_pendingChatAccount = accountId;
    m_pendingChatName = name;
    return;
  }
  page->runJavaScript(ChatNav::openChatByNameScript(name));
}

QString MainWindow::accountTabTooltip(const Account &acc) const {
  QString token;
  if (pageOf(acc))
    token = QUrlQuery(pageOf(acc)->url()).queryItemValue(QStringLiteral("v"));
  return accountTabTooltipText(acc.waVersion, token);
}

// Rebuild the tab labels: the account name, plus its own unread count, plus a
// trailing "+" tab. Cheap, and called only when something actually changed.
void MainWindow::refreshAccountTabs() {
  updateGridCaptions();
  // The Tabbed / Grid view options only make sense with more than one account.
  const bool multi = m_accounts.size() > 1;
  if (m_viewTabsAction)
    m_viewTabsAction->setVisible(multi);
  if (m_viewGridAction)
    m_viewGridAction->setVisible(multi);
  // In grid mode the strips are hidden and the views live in the tiles, so only
  // the captions (refreshed above) need updating.
  if (m_viewMode == ViewMode::Grid)
    return;
  if (!m_accountBar)
    return;
  QSignalBlocker block(m_accountBar);

  // Only accounts hosted in the main window get a tab here; detached ones live
  // in their own windows. Each tab stores its account index in tab data.
  QList<int> docked;
  for (int i = 0; i < m_accounts.size(); ++i)
    if (!m_accounts[i].window)
      docked.append(i);

  // Match the tab count incrementally (add/remove only when the set changes),
  // so a plain unread-count update just relabels and never flickers.
  const int wanted = docked.size() + 1; // + the trailing "+" affordance
  while (m_accountBar->count() > wanted)
    m_accountBar->removeTab(m_accountBar->count() - 1);
  while (m_accountBar->count() < wanted)
    m_accountBar->addTab(QString());

  int activeTab = 0;
  for (int t = 0; t < docked.size(); ++t) {
    const int i = docked[t];
    QString label = m_accounts[i].name;
    if (m_accounts[i].unread > 0)
      label += QStringLiteral("  (%1)").arg(m_accounts[i].unread);
    m_accountBar->setTabText(t, label);
    m_accountBar->setTabData(t, m_accounts[i].id); // stable id, drag identity
    m_accountBar->setTabToolTip(t, accountTabTooltip(m_accounts[i]));
    if (i == m_activeAccount)
      activeTab = t;
  }
  const int plus = docked.size();
  m_accountBar->setTabText(plus, QStringLiteral("  +  "));
  m_accountBar->setTabData(plus, QVariant()); // no data marks the "+" tab
  m_accountBar->setTabToolTip(plus, tr("Add another account"));

  // With more than one account the strip is the only way to reach the others,
  // so it is always up in tabbed mode (Grid hides it separately). With a single
  // account it is a row of chrome showing one tab, and the "+" it carries is
  // reachable from Ctrl+K, the command palette and the Add-account action — so
  // it stays out of the way unless asked for.
  m_accountBar->setVisible(viewMode() != ViewMode::Grid &&
                           (docked.size() > 1 || alwaysShowAccountTabs()));
  m_accountBar->setCurrentIndex(activeTab);

  refreshDetachedStrips(); // keep every detached window's strip in step too
}

void MainWindow::updateTrayUnread() {
  refreshRecentUnread(); // keep the tray's recent-unread submenu in step (#3)
  int total = 0;
  for (const Account &a : m_accounts)
    total += a.unread;

  if (QFile f(unreadCountFile());
      f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QByteArray::number(total));
    f.close();
  }

  if (total > 0) {
    // Chats, not messages: what is summed here is one per conversation with
    // something unread in it.
    m_restoreAction->setText(tr("Restore") + " | " + QString::number(total) +
                             " " + (total > 1 ? tr("chats") : tr("chat")));
    m_systemTrayIcon->setIcon(getTrayIcon(total));
    // The badge stops at "99+" because a third digit is a few pixels wide once the
    // panel has scaled the icon down; the tooltip is where the real number fits.
    //
    // It counts what the tabs count — the same figures, so the same choice of
    // muted in or out and archived in or out — and it says both numbers rather
    // than only the badge's one, split by muted where the page could tell us:
    // a hundred messages waiting in muted chats and three in the rest is a
    // different morning from the other way round.
    UnreadBreakdown sum;
    sum.mutedKnown = true;
    // The muted setting is global, so every account's detail agrees on it.
    sum.mutedInTotal = unreadCountIncludesMuted();
    for (const Account &a : m_accounts) {
      sum.chats += a.unreadDetail.chats;
      sum.messages += a.unreadDetail.messages;
      sum.mutedChats += a.unreadDetail.mutedChats;
      sum.mutedMessages += a.unreadDetail.mutedMessages;
      // One account that cannot split its count makes the whole split unsound.
      if (a.unreadDetail.chats > 0 && !a.unreadDetail.mutedKnown)
        sum.mutedKnown = false;
    }
    m_systemTrayIcon->setToolTip(trayTooltipText(sum));
    setWindowIcon(getTrayIcon(total));
  } else {
    m_restoreAction->setText(tr("Restore"));
    // Route the idle icon through getTrayIcon(0) too, so it honours the
    // monochrome choice and the connection state instead of always showing the
    // fixed colour icon (issue #14: monochrome appeared to do nothing whenever
    // there were no unread messages).
    m_systemTrayIcon->setIcon(getTrayIcon(0));
    m_systemTrayIcon->setToolTip(trayTooltipText(UnreadBreakdown{}));
    setWindowIcon(appWindowIcon());
  }

  updateLauncherBadge(total);
}

// Broadcast the unread total as a taskbar badge using the com.canonical.Unity
// LauncherEntry protocol. It is a plain session-bus signal — no libunity, no
// dependency — that KDE Plasma's task manager and GNOME's Dash-to-Dock (among
// others) listen for and paint as a count on the app's launcher/task button
// (issue #122). Desktops that don't implement it simply ignore the signal.
void MainWindow::updateLauncherBadge(int count) {
#ifdef Q_OS_LINUX
  QDBusMessage signal = QDBusMessage::createSignal(
      QStringLiteral("/net/shakaran/whatly/LauncherEntry"),
      QStringLiteral("com.canonical.Unity.LauncherEntry"),
      QStringLiteral("Update"));
  QVariantMap props;
  props.insert(QStringLiteral("count"), static_cast<qlonglong>(count));
  props.insert(QStringLiteral("count-visible"), count > 0);
  signal << QStringLiteral("application://net.shakaran.whatly.desktop")
         << props;
  QDBusConnection::sessionBus().send(signal);
#else
  Q_UNUSED(count);
#endif
}

void MainWindow::promptAddAccount(DetachedAccountWindow *target) {
  // Put the strip that was clicked back onto a real account: the click landed on
  // "+", which is not a page, and it must not be left selected if the dialog is
  // cancelled.
  if (target)
    refreshDetachedStrips();
  else
    setActiveAccount(m_activeAccount);

  bool ok = false;
  const QString name =
      QInputDialog::getText(this, tr("Add account"),
                            tr("Name for the new account:"), QLineEdit::Normal,
                            tr("Account %1").arg(m_accounts.size() + 1), &ok);
  if (!ok || name.trimmed().isEmpty())
    return;

  // A random, stable id keeps the storage directory name independent of the
  // display name, so renaming an account never moves its session.
  const QString id = Utils::generateRandomId(8);
  addAccount(id, name.trimmed(), true);
  saveAccounts();
  // The new account joins the window it was asked for. An explicit target is the
  // strip whose "+" was clicked; without one, fall back to the focused window,
  // which is what the tray action and the command palette go through.
  DetachedAccountWindow *focused =
      target ? target
             : (m_focusOrder.isEmpty() ? nullptr : m_focusOrder.first());
  if (focused) {
    moveAccountToWindow(id, focused, 1 << 20); // append into that window
  } else {
    refreshAccountTabs();
    setActiveAccount(accountIndexForId(id));
  }
  maybeShowDetachHint();
}

void MainWindow::renameAccount(int index) {
  if (index < 0 || index >= m_accounts.size())
    return;
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, tr("Rename account"), tr("Account name:"), QLineEdit::Normal,
      m_accounts[index].name, &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  m_accounts[index].name = name.trimmed();
  saveAccounts();
  refreshAccountTabs(); // updates the label wherever the account is hosted
}

void MainWindow::removeAccount(int index) {
  // Only the default account (id "") is protected — it is the app's own session.
  // Any other account is removable regardless of its position (the old
  // `index <= 0` guard wrongly blocked whatever sat at m_accounts[0]).
  if (index < 0 || index >= m_accounts.size() || m_accounts[index].id.isEmpty())
    return;

  const QString activeId =
      (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
          ? m_accounts[m_activeAccount].id
          : QString();

  Account account = m_accounts.takeAt(index);
  DetachedAccountWindow *win = account.window;
  if (account.view) {
    account.view->setParent(nullptr); // out of whichever stack held it
    account.view->deleteLater();
  }
  // If that emptied a detached window, close it.
  if (win) {
    bool empty = true;
    for (const Account &a : m_accounts)
      if (a.window == win) {
        empty = false;
        break;
      }
    if (empty)
      destroyDetachedWindow(win);
  }

  // Restore the active account by id (indices shifted; it may even have been the
  // removed one), keeping it a docked account.
  m_activeAccount = accountIndexForId(activeId);
  if (m_activeAccount < 0 || m_accounts[m_activeAccount].window) {
    m_activeAccount = 0;
    for (int i = 0; i < m_accounts.size(); ++i)
      if (!m_accounts[i].window) {
        m_activeAccount = i;
        break;
      }
  }

  saveAccounts();
  refreshAccountTabs();
  setActiveAccount(m_activeAccount);
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  updateTrayUnread();
}

// Menu / main-strip tear-off entry point.
void MainWindow::detachAccount(int index, QPoint dropGlobalPos) {
  if (index < 0 || index >= m_accounts.size())
    return;
  tearOutToNewWindow(m_accounts[index].id, dropGlobalPos);
}

// Create a fresh detached window and wire its strip's signals into the single
// coordinator (this MainWindow). It starts empty; the caller moves accounts in.
DetachedAccountWindow *MainWindow::createDetachedWindow() {
  auto *win = new DetachedAccountWindow;
  AccountTabBar *bar = win->bar();
  // Switch which account this window shows.
  connect(bar, &QTabBar::currentChanged, this, [this, win](int t) {
    if (t < 0)
      return;
    const QVariant d = win->bar()->tabData(t);
    if (!d.isValid()) { // the "+" affordance, same as on the main strip
      promptAddAccount(win);
      return;
    }
    const int idx = accountIndexForId(d.toString());
    if (idx >= 0 && m_accounts[idx].view) {
      win->stack()->setCurrentWidget(m_accounts[idx].view);
      win->setWindowTitle(accountTitle(idx));
    }
  });
  // A tab dropped onto this window's strip -> move that account in here.
  connect(bar, &AccountTabBar::accountDropped, this,
          [this, win](const QString &id, int slot) {
            m_tabDropHandledByStrip = true; // consumed here; skip geometry routing
            QTimer::singleShot(0, this, [this, win, id, slot]() {
              moveAccountToWindow(id, win, slot);
            });
          });
  // A tab drag ended over this window's strip -> route it the same way.
  connect(bar, &AccountTabBar::dragReleased, this,
          [this](const QString &id, const QPoint &pos) {
            QTimer::singleShot(0, this,
                               [this, id, pos]() { onTabDragReleased(id, pos); });
          });
  // A tab slid within this window's strip -> re-derive its order.
  connect(bar, &QTabBar::tabMoved, this, [this, win](int, int) {
    if (m_reorderingTabs)
      return;
    reorderWindowFromStrip(win);
  });
  // Right-click a tab in this window: rename / tear out / remove the account.
  connect(bar, &QWidget::customContextMenuRequested, this,
          [this, win](const QPoint &pos) {
            const int tabIndex = win->bar()->tabAt(pos);
            if (tabIndex < 0)
              return;
            const QVariant d = win->bar()->tabData(tabIndex);
            if (!d.isValid())
              return;
            const int index = accountIndexForId(d.toString());
            if (index < 0)
              return;
            int here = 0;
            for (const Account &a : m_accounts)
              if (a.window == win)
                ++here;
            QMenu menu;
            QAction *rename = menu.addAction(tr("Rename…"));
            QAction *detach = menu.addAction(tr("Open in own window"));
            detach->setEnabled(here > 1); // else it is already alone here
            menu.addSeparator();
            QAction *remove = menu.addAction(tr("Remove account"));
            remove->setEnabled(!m_accounts[index].id.isEmpty() &&
                               m_accounts.size() > 1);
            QAction *chosen = menu.exec(win->bar()->mapToGlobal(pos));
            if (chosen == rename)
              renameAccount(index);
            else if (chosen == detach)
              tearOutToNewWindow(m_accounts[index].id, QPoint(-1, -1));
            else if (chosen == remove)
              removeAccount(index);
          });
  // Becoming active makes this window "main" (front of the focus order).
  connect(win, &DetachedAccountWindow::activated, this,
          [this, win]() { noteWindowFocused(win); });
  // The application-wide actions are carried by the main window, and Qt only
  // fires a shortcut while a window carrying it is up and active — so with the
  // main window hidden in the tray and this window in front, Ctrl+P and the rest
  // did nothing at all, which is the "one window is special" this PR is about.
  // Attaching the same actions here fixes that: a shortcut fires if ANY window
  // holding the action qualifies.
  //
  // Only the actions that are about the application. The ones that act on "the
  // current account" are deliberately left out: reload, zoom and fullscreen would
  // silently mean the main window's account rather than the one being looked at,
  // which is worse than the shortcut not working.
  // Find is the one account action that is safe to share, because it asks
  // focusedAccountIndex() which account the window with the keyboard is showing
  // instead of assuming the app-wide one. Reload, zoom and fullscreen have no
  // such answer yet, so they stay out.
  for (QAction *shared :
       {m_settingsAction, m_commandPaletteAction, m_aboutAction, m_quitAction,
        m_toggleThemeAction, m_muteAction, m_lockAction,
        m_scheduledMessagesAction, m_addAccountAction, m_chatListStripAction,
        m_findChatAction})
    if (shared)
      win->addAction(shared);
  // Moving/resizing the window updates the saved arrangement (debounced).
  connect(win, &DetachedAccountWindow::geometryChanged, this, [this]() {
    if (m_layoutSaveTimer)
      m_layoutSaveTimer->start();
  });
  // Minimised, put away or brought back: its accounts are unloaded or built
  // again with it, the same as the main window's (issue #25). It is a peer window
  // here as everywhere else — an account being watched on its own is exactly the
  // one that must not be unloaded because the main window went away.
  connect(win, &DetachedAccountWindow::visibilityChanged, this,
          [this]() { noteWindowVisibilityChanged(); });
  // Closing the window docks its accounts back into the front-most survivor.
  connect(win, &DetachedAccountWindow::closed, this,
          [this, win]() { closeDetachedWindow(win); });
  return win;
}

void MainWindow::noteWindowFocused(DetachedAccountWindow *win) {
  m_focusOrder.removeAll(win);
  m_focusOrder.prepend(win); // most-recently-focused first; front is "main"
  refreshWindowsMenu();      // the numbering IS this order
}

void MainWindow::destroyDetachedWindow(DetachedAccountWindow *win) {
  if (!win)
    return;
  m_focusOrder.removeAll(win);
  refreshWindowsMenu(); // one window fewer to offer
  win->disconnect(this);
  win->deleteLater();
}

// A tab drag ended. If a strip already consumed it (positional dock/move), the
// flag is set and we do nothing. Otherwise route by geometry: dropped on another
// Whatly window's body → move it there (appended after its rightmost tab); on
// its own window → leave it; clear of every window → move/tear off at the cursor.
void MainWindow::onTabDragReleased(const QString &id, QPoint globalPos) {
  if (m_tabDropHandledByStrip) {
    m_tabDropHandledByStrip = false;
    return;
  }

  const int idx = accountIndexForId(id);
  if (idx < 0)
    return;
  DetachedAccountWindow *cur = m_accounts[idx].window;

  QWidget *under = QApplication::widgetAt(globalPos);
  QWidget *top = under ? under->window() : nullptr;
  DetachedAccountWindow *targetWin = qobject_cast<DetachedAccountWindow *>(top);
  const int append = 1 << 20; // a slot past the end → rightmost

  if (top == this) { // dropped on the main window's body
    if (cur != nullptr)
      dockAccountToMainAt(id, append);
    return; // already in main → nothing to do
  }
  if (targetWin) { // dropped on a detached window's body
    if (cur == targetWin)
      return; // already there
    // moveAccountToWindow absorbs the target into the main window when this is
    // the main window's last tab, so the main window never empties.
    moveAccountToWindow(id, targetWin, append);
    return;
  }
  // Clear of every Whatly window → tear off a new one (or, for a lone tab, move
  // its whole window) at the cursor.
  tearOutToNewWindow(id, globalPos);
}

// Tear the account with `id` off into a brand-new window near `pos`, wherever it
// currently lives. If it is the ONLY tab in its window there is nothing to tear
// off: just move that whole window to the drop point (keeping its size), so the
// tab lands under the cursor and no window is created or emptied.
void MainWindow::tearOutToNewWindow(const QString &id, QPoint pos) {
  const int idx = accountIndexForId(id);
  if (idx < 0 || !m_accounts[idx].view)
    return;
  DetachedAccountWindow *src = m_accounts[idx].window; // null = the main window
  int siblings = 0;
  for (const Account &a : m_accounts)
    if (a.window == src)
      ++siblings;
  if (siblings < 2) {
    if (pos.x() >= 0)
      (src ? static_cast<QWidget *>(src) : static_cast<QWidget *>(this))
          ->move(pos - QPoint(40, 20));
    return;
  }
  auto *win = createDetachedWindow();
  if (pos.x() >= 0)
    win->move(pos - QPoint(40, 20));
  moveAccountToWindow(id, win, 0); // reparents the view in, shows + raises it
}

// The one account mover for a DETACHED target (the main window is
// dockAccountToMainAt's job). Reparents the view, updates ownership, reorders
// within the target, closes an emptied source window, and focuses the result.
void MainWindow::moveAccountToWindow(const QString &id,
                                     DetachedAccountWindow *targetWin, int slot) {
  if (!targetWin) {
    dockAccountToMainAt(id, slot);
    return;
  }
  const int idx0 = accountIndexForId(id);
  if (idx0 < 0 || !m_accounts[idx0].view)
    return;
  DetachedAccountWindow *sourceWin = m_accounts[idx0].window;

  // If this is the main window's LAST docked tab being dropped into a detached
  // window, moving it out would empty the main window. Instead the main window
  // absorbs the target (takes its place and its tabs); the target is destroyed.
  if (!sourceWin) {
    int dockedMain = 0;
    for (const Account &a : m_accounts)
      if (!a.window)
        ++dockedMain;
    if (dockedMain == 1) {
      absorbWindowIntoMain(targetWin, id, slot);
      return;
    }
  }

  WebView *view = m_accounts[idx0].view;
  const bool sameWindow = (sourceWin == targetWin);

  int origSlot = -1;
  if (sameWindow)
    for (int i = 0, s = 0; i < m_accounts.size(); ++i) {
      if (m_accounts[i].window != targetWin)
        continue;
      if (i == idx0) {
        origSlot = s;
        break;
      }
      ++s;
    }

  const QString mainActiveId =
      (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
          ? m_accounts[m_activeAccount].id
          : QString();

  targetWin->stack()->addWidget(view); // reparents the view into the target
  m_accounts[idx0].window = targetWin;

  if (sameWindow && origSlot >= 0 && slot > origSlot)
    --slot;

  Account acc = m_accounts.takeAt(idx0);
  QList<int> members;
  for (int i = 0; i < m_accounts.size(); ++i)
    if (m_accounts[i].window == targetWin)
      members.append(i);
  int target;
  if (slot <= 0)
    target = members.isEmpty() ? m_accounts.size() : members.first();
  else if (slot >= members.size())
    target = members.isEmpty() ? m_accounts.size() : members.last() + 1;
  else
    target = members[slot];
  m_accounts.insert(target, acc);

  // The main window's active account may have just moved out; keep it valid.
  m_activeAccount = accountIndexForId(mainActiveId);
  if (m_activeAccount < 0 || m_accounts[m_activeAccount].window) {
    m_activeAccount = 0;
    for (int i = 0; i < m_accounts.size(); ++i)
      if (!m_accounts[i].window) {
        m_activeAccount = i;
        break;
      }
  }

  // Close the source window if it is now empty.
  if (sourceWin && !sameWindow) {
    bool empty = true;
    for (const Account &a : m_accounts)
      if (a.window == sourceWin) {
        empty = false;
        break;
      }
    if (empty)
      destroyDetachedWindow(sourceWin);
  }

  refreshAccountTabs(); // main + all detached strips
  setActiveAccount(m_activeAccount);

  // Focus the moved account in its target window.
  targetWin->stack()->setCurrentWidget(view);
  {
    AccountTabBar *bar = targetWin->bar();
    QSignalBlocker b(bar);
    for (int t = 0; t < bar->count(); ++t)
      if (bar->tabData(t).toString() == id) {
        bar->setCurrentIndex(t);
        break;
      }
  }
  if (const int mi = accountIndexForId(id); mi >= 0)
    targetWin->setWindowTitle(accountTitle(mi));
  targetWin->show();
  targetWin->raise();
  targetWin->activateWindow();
  if (view)
    view->setFocus(Qt::OtherFocusReason);
  // A tear-off/dock happens mid-interaction (a drag, or a menu closing), and
  // Windows tends to keep focus on the source window then. Re-assert focus once
  // the current event has unwound so the new/target window really comes forward.
  QTimer::singleShot(0, targetWin, [targetWin]() {
    targetWin->raise();
    targetWin->activateWindow();
  });
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  updateTrayUnread();
  saveAccounts();
}

// The main window's last tab was dropped into `win`. We never leave the main
// window empty, and cannot destroy it (it owns the tray and app-level state), so
// the main window ABSORBS `win`: it takes `win`'s geometry and all of its tabs,
// with the dragged account inserted at `slot` (rightmost when past the end), in
// that order. `win` is then destroyed. To the user the window they dropped onto
// simply "becomes" the main window, tabs in the intended order.
void MainWindow::absorbWindowIntoMain(DetachedAccountWindow *win,
                                      const QString &movedId, int slot) {
  if (!win)
    return;
  // The merged tab order: `win`'s accounts, with the dragged account inserted at
  // the drop slot.
  QStringList order;
  for (const Account &a : m_accounts)
    if (a.window == win)
      order << a.id;
  order.insert(qBound(0, slot, order.size()), movedId);

  const QRect geom = win->geometry();

  // Re-home each of those accounts into the main window: reparent its view into
  // the main stack and clear its window pointer.
  for (const QString &aid : order) {
    const int i = accountIndexForId(aid);
    if (i < 0)
      continue;
    if (m_accounts[i].view)
      m_accountStack->addWidget(m_accounts[i].view);
    m_accounts[i].window = nullptr;
  }

  // Reorder m_accounts so the (now all-docked) merged accounts follow `order`;
  // accounts still in OTHER detached windows keep their positions.
  QList<Account> rebuilt;
  int di = 0;
  for (const Account &a : m_accounts) {
    if (!a.window && di < order.size()) {
      const int i = accountIndexForId(order[di++]);
      rebuilt.append(m_accounts[i]);
    } else {
      rebuilt.append(a);
    }
  }
  if (rebuilt.size() == m_accounts.size())
    m_accounts = rebuilt;

  win->hide();
  destroyDetachedWindow(win); // views already reparented out; safe to dispose

  // The main window takes the absorbed window's place and shows the dragged tab
  // (the deliberate drop target) as active.
  setGeometry(geom);
  m_activeAccount = accountIndexForId(movedId);
  if (m_activeAccount < 0)
    m_activeAccount = 0;
  refreshAccountTabs();
  setActiveAccount(m_activeAccount);
  show();
  raise();
  activateWindow();
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  updateTrayUnread();
  saveAccounts();
}

// A detached window is closing: dock all of its accounts back into the main
// window (without stealing focus), then dispose of the window.
void MainWindow::closeDetachedWindow(DetachedAccountWindow *win) {
  if (!win)
    return;
  win->disconnect(this); // no further signals while we dismantle it
  m_focusOrder.removeAll(win);
  // Dock the accounts into the most-recently-focused surviving window (front of
  // the focus order); nullptr means the main window. No focus steal — the tabs
  // arrive in the background, since the window was closed to get it out of sight.
  DetachedAccountWindow *target =
      m_focusOrder.isEmpty() ? nullptr : m_focusOrder.first();
  if (target == win)
    target = nullptr;
  QStringList ids;
  for (const Account &a : m_accounts)
    if (a.window == win)
      ids << a.id;
  for (const QString &id : ids) {
    const int idx = accountIndexForId(id);
    if (idx < 0 || !m_accounts[idx].view)
      continue;
    if (target)
      target->stack()->addWidget(m_accounts[idx].view);
    else
      m_accountStack->addWidget(m_accounts[idx].view);
    m_accounts[idx].window = target;
  }
  win->deleteLater();
  refreshAccountTabs();
  setActiveAccount(m_activeAccount);
  if (m_viewMode == ViewMode::Grid)
    relayoutGrid();
  updateTrayUnread();
  saveAccounts();
}

// Rebuild every detached window's tab strip from m_accounts (labels, order,
// which tab is current), keeping each window's shown view in step.
void MainWindow::refreshDetachedStrips() {
  QSet<DetachedAccountWindow *> wins;
  for (const Account &a : m_accounts)
    if (a.window)
      wins.insert(a.window);
  for (DetachedAccountWindow *win : wins) {
    AccountTabBar *bar = win->bar();
    if (!bar)
      continue;
    QSignalBlocker block(bar);
    QList<int> members;
    for (int i = 0; i < m_accounts.size(); ++i)
      if (m_accounts[i].window == win)
        members.append(i);
    // Mirror the main strip: one tab per member plus a trailing "+", so adding
    // an account is reachable from a detached window too rather than only from
    // the main one.
    const int wanted = members.size() + 1;
    while (bar->count() > wanted)
      bar->removeTab(bar->count() - 1);
    while (bar->count() < wanted)
      bar->addTab(QString());
    QWidget *current = win->stack()->currentWidget();
    int activeTab = 0;
    for (int t = 0; t < members.size(); ++t) {
      const int i = members[t];
      QString label = m_accounts[i].name;
      if (m_accounts[i].unread > 0)
        label += QStringLiteral("  (%1)").arg(m_accounts[i].unread);
      bar->setTabText(t, label);
      bar->setTabData(t, m_accounts[i].id);
      bar->setTabToolTip(t, accountTabTooltip(m_accounts[i]));
      if (m_accounts[i].view == current)
        activeTab = t;
    }
    const int plus = members.size();
    bar->setTabText(plus, QStringLiteral("  +  "));
    bar->setTabData(plus, QVariant()); // no data marks the "+" tab
    bar->setTabToolTip(plus, tr("Add another account"));
    if (!members.isEmpty()) {
      bar->setCurrentIndex(activeTab);
      const int i = members[qBound(0, activeTab, members.size() - 1)];
      win->stack()->setCurrentWidget(m_accounts[i].view);
      win->setWindowTitle(accountTitle(i));
    }
  }
}

// A one-shot tip, shown the first time the user ends up with more than one
// account (which is also the first time the tab bar becomes visible), pointing
// out that a tab can be pulled into its own window.
void MainWindow::maybeShowDetachHint() {
  QSettings &s = SettingsManager::instance().settings();
  if (s.value(QStringLiteral("hints/detachTabShown"), false).toBool())
    return;
  s.setValue(QStringLiteral("hints/detachTabShown"), true);
  QMessageBox::information(
      this, tr("Tip: give an account its own window"),
      tr("You now have more than one account, shown as tabs along the top.\n\n"
         "You can pull any account out into its own window: right-click its tab "
         "and choose “Open in own window”. Close that window to dock the "
         "account back as a tab."));
}

// The accounts list lives in the (process-level) settings, so it is per
// --profile: launching --profile=work has its own separate set of tabs. Stored
// as parallel id/name lists; the default account is implicit and always first.
void MainWindow::saveAccounts() {
  if (m_loadingLayout || m_isQuitting)
    return; // a restore is in progress, or we are collapsing windows to quit —
            // either way, don't clobber the saved state
  // Persist the accounts IN ORDER, including the default account's position. Its
  // real id is the empty string, which Windows' registry string lists can
  // silently truncate, so it is written as a token instead.
  QStringList ids, names;
  for (const Account &a : m_accounts) {
    ids << (a.id.isEmpty() ? QStringLiteral("__default__") : a.id);
    names << a.name;
  }
  QSettings &s = SettingsManager::instance().settings();
  s.setValue(QStringLiteral("accounts/ids"), ids);
  s.setValue(QStringLiteral("accounts/names"), names);
  saveWindowLayout();
}

// Alongside the account list, record where each account is shown: "main" or the
// index of a detached window, plus each detached window's geometry and active
// tab. This is saved ALWAYS (on every tab move and window geometry change), not
// only when the user opted in — the "rememberWindowLayout" toggle only decides
// whether restoreWindowLayout REBUILDS the windows or collapses them on start.
// The default account's empty id is written as a token (see saveAccounts).
void MainWindow::saveWindowLayout() {
  if (m_loadingLayout || m_isQuitting)
    return; // mid-restore, or collapsing windows to quit: don't clobber the save
  QSettings &s = SettingsManager::instance().settings();
  s.beginGroup(QStringLiteral("windowLayout"));
  const QString kDefault = QStringLiteral("__default__");
  const auto token = [&](const QString &id) {
    return id.isEmpty() ? kDefault : id;
  };

  // Detached windows in a stable first-seen order.
  QList<DetachedAccountWindow *> wins;
  for (const Account &a : m_accounts)
    if (a.window && !wins.contains(a.window))
      wins.append(a.window);

  // assign[i] tells where account i (in m_accounts / accounts-ids order) lives.
  QStringList assign;
  for (const Account &a : m_accounts)
    assign << (a.window ? QStringLiteral("d%1").arg(wins.indexOf(a.window))
                        : QStringLiteral("main"));

  QStringList geoms, actives;
  for (DetachedAccountWindow *w : wins) {
    const QRect g = w->geometry();
    geoms << QStringLiteral("%1,%2,%3,%4")
                 .arg(g.x())
                 .arg(g.y())
                 .arg(g.width())
                 .arg(g.height());
    QString activeId = kDefault;
    QWidget *cur = w->stack() ? w->stack()->currentWidget() : nullptr;
    for (const Account &a : m_accounts)
      if (a.window == w && a.view == cur) {
        activeId = token(a.id);
        break;
      }
    actives << activeId;
  }

  QString mainActive = kDefault;
  if (m_activeAccount >= 0 && m_activeAccount < m_accounts.size() &&
      !m_accounts[m_activeAccount].window)
    mainActive = token(m_accounts[m_activeAccount].id);

  s.setValue(QStringLiteral("present"), !wins.isEmpty());
  s.setValue(QStringLiteral("assign"), assign);
  s.setValue(QStringLiteral("detachedGeoms"), geoms);
  s.setValue(QStringLiteral("detachedActives"), actives);
  s.setValue(QStringLiteral("mainActive"), mainActive);

  // Grid view: remember the user's dragged tile sizes + grid window size, but
  // only while they have actually customized it (otherwise the tiles are
  // distributed equally on every entry).
  s.setValue(QStringLiteral("gridCustomized"), m_gridCustomized);
  if (m_gridCustomized) {
    s.setValue(QStringLiteral("gridGeom"),
               QStringLiteral("%1,%2,%3,%4")
                   .arg(m_gridSavedGeom.x())
                   .arg(m_gridSavedGeom.y())
                   .arg(m_gridSavedGeom.width())
                   .arg(m_gridSavedGeom.height()));
    QStringList rows, cols;
    for (int v : m_gridSavedRows)
      rows << QString::number(v);
    for (int v : m_gridSavedCols)
      cols << QString::number(v);
    s.setValue(QStringLiteral("gridRows"), rows);
    s.setValue(QStringLiteral("gridCols"), cols);
  } else {
    s.remove(QStringLiteral("gridGeom"));
    s.remove(QStringLiteral("gridRows"));
    s.remove(QStringLiteral("gridCols"));
  }
  s.endGroup();
}

// Order the (single-window) tabs as if each non-main window had been closed in
// ordinal order: the main window's accounts first (in their saved order), then
// window d0's, then d1's, and so on. Used when we come up collapsed (the toggle
// is off, the crash guard fired, or the save no longer matches), so the tab
// positions the user arranged are preserved even without separate windows. All
// accounts are already docked in the main window at this point; only the order
// of m_accounts changes.
void MainWindow::collapseToOrdinalOrder(const QStringList &assign) {
  if (assign.size() != m_accounts.size())
    return;
  int maxK = -1;
  for (const QString &a : assign)
    if (a.startsWith(QLatin1Char('d'))) {
      bool ok = false;
      const int k = a.mid(1).toInt(&ok);
      if (ok && k > maxK)
        maxK = k;
    }
  QStringList tokens;
  tokens << QStringLiteral("main");
  for (int k = 0; k <= maxK; ++k)
    tokens << QStringLiteral("d%1").arg(k);

  QList<Account> rebuilt;
  for (const QString &tok : tokens)
    for (int i = 0; i < m_accounts.size(); ++i)
      if (assign.at(i) == tok)
        rebuilt.append(m_accounts[i]);
  // Paranoia: append anything an unrecognised token left out, unchanged.
  if (rebuilt.size() != m_accounts.size())
    for (int i = 0; i < m_accounts.size(); ++i) {
      bool seen = false;
      for (const Account &a : rebuilt)
        if (a.id == m_accounts[i].id) {
          seen = true;
          break;
        }
      if (!seen)
        rebuilt.append(m_accounts[i]);
    }
  if (rebuilt.size() == m_accounts.size())
    m_accounts = rebuilt;
}

// Apply the always-saved window arrangement after loadAccounts has created all
// accounts (initially docked in the main window). The "rememberWindowLayout"
// toggle decides whether to REBUILD the detached windows or come up collapsed
// (preserving tab order via collapseToOrdinalOrder). A crash guard
// (restoreInProgress) skips the rebuild once after a run that didn't settle,
// without ever discarding the saved layout.
void MainWindow::restoreWindowLayout() {
  QSettings &s = SettingsManager::instance().settings();
  s.beginGroup(QStringLiteral("windowLayout"));
  const bool present = s.value(QStringLiteral("present"), false).toBool();
  const bool crashed =
      s.value(QStringLiteral("restoreInProgress"), false).toBool();
  const QStringList assign = s.value(QStringLiteral("assign")).toStringList();
  const QStringList geoms = s.value(QStringLiteral("detachedGeoms")).toStringList();
  const QStringList actives =
      s.value(QStringLiteral("detachedActives")).toStringList();
  const QString mainActive = s.value(QStringLiteral("mainActive")).toString();
  const bool gridCustomized =
      s.value(QStringLiteral("gridCustomized"), false).toBool();
  const QString gridGeom = s.value(QStringLiteral("gridGeom")).toString();
  const QStringList gridRows = s.value(QStringLiteral("gridRows")).toStringList();
  const QStringList gridCols = s.value(QStringLiteral("gridCols")).toStringList();
  s.endGroup();

  const bool remember =
      s.value(QStringLiteral("rememberWindowLayout"), false).toBool();

  // Grid tile sizes are remembered only when opted in — independent of whether
  // any window was detached, so load them before the present() early-out.
  if (remember && gridCustomized) {
    m_gridCustomized = true;
    const QStringList p = gridGeom.split(QLatin1Char(','));
    if (p.size() == 4)
      m_gridSavedGeom =
          QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt());
    m_gridSavedRows.clear();
    for (const QString &v : gridRows)
      m_gridSavedRows << v.toInt();
    m_gridSavedCols.clear();
    for (const QString &v : gridCols)
      m_gridSavedCols << v.toInt();
  }

  if (!present)
    return; // no multi-window layout was ever saved; the single window is fine

  if (crashed) {
    // The previous rebuild never settled (most likely it crashed). Skip it once
    // and clear the flag so the next start tries again; the layout is untouched.
    s.setValue(QStringLiteral("windowLayout/restoreInProgress"), false);
    s.sync();
  }

  const bool sizeOk = (assign.size() == m_accounts.size());
  if (!remember || crashed || !sizeOk) {
    // Come up as one window, tabs in the recorded ordinal order.
    collapseToOrdinalOrder(assign);
    return;
  }

  // ── Rebuild the detached windows ──────────────────────────────────────────
  // Arm the crash guard before we build anything; disarmed by the settle timer.
  s.setValue(QStringLiteral("windowLayout/restoreInProgress"), true);
  s.sync();

  m_loadingLayout = true;
  const QString kDefault = QStringLiteral("__default__");

  QList<DetachedAccountWindow *> wins;
  for (int k = 0; k < geoms.size(); ++k)
    wins.append(createDetachedWindow());

  // Move each account into its saved window (direct, low-level: the normal
  // movers reorder/refresh/save, which we neither need nor want mid-restore).
  for (int i = 0; i < m_accounts.size() && i < assign.size(); ++i) {
    const QString a = assign.at(i);
    if (!a.startsWith(QLatin1Char('d')))
      continue; // stays docked in the main window
    bool ok = false;
    const int k = a.mid(1).toInt(&ok);
    if (!ok || k < 0 || k >= wins.size())
      continue;
    if (m_accounts[i].view)
      wins[k]->stack()->addWidget(m_accounts[i].view); // reparents into it
    m_accounts[i].window = wins[k];
  }

  // Safety against a corrupt/stale save: the main window must keep at least one
  // tab. If everything got assigned away, dock the first account back.
  bool anyDocked = false;
  for (const Account &a : m_accounts)
    if (!a.window) {
      anyDocked = true;
      break;
    }
  if (!anyDocked && !m_accounts.isEmpty()) {
    if (m_accounts[0].view)
      m_accountStack->addWidget(m_accounts[0].view);
    m_accounts[0].window = nullptr;
  }

  // Geometry + active tab for each detached window.
  for (int k = 0; k < wins.size(); ++k) {
    const QStringList p = geoms.value(k).split(QLatin1Char(','));
    if (p.size() == 4)
      wins[k]->setGeometry(p[0].toInt(), p[1].toInt(), p[2].toInt(),
                           p[3].toInt());
    const QString act = actives.value(k);
    const QString actId = (act == kDefault) ? QString() : act;
    const int ai = accountIndexForId(actId);
    if (ai >= 0 && m_accounts[ai].window == wins[k] && m_accounts[ai].view)
      wins[k]->stack()->setCurrentWidget(m_accounts[ai].view);
  }

  // Keep the main window's active tab a docked account.
  const QString mAct = (mainActive == kDefault) ? QString() : mainActive;
  const int mi = accountIndexForId(mAct);
  if (mi >= 0 && !m_accounts[mi].window)
    m_activeAccount = mi;

  // Never show an empty window: drop any detached window a corrupt or stale save
  // left with no accounts.
  for (int k = wins.size() - 1; k >= 0; --k) {
    bool has = false;
    for (const Account &a : m_accounts)
      if (a.window == wins[k]) {
        has = true;
        break;
      }
    if (!has)
      destroyDetachedWindow(wins[k]);
  }

  m_loadingLayout = false;

  // Once the app has run a few seconds without crashing, disarm the guard.
  QTimer::singleShot(4000, this, [this]() {
    SettingsManager::instance().settings().setValue(
        QStringLiteral("windowLayout/restoreInProgress"), false);
  });
}

void MainWindow::loadAccounts() {
  QSettings &s = SettingsManager::instance().settings();
  const QStringList ids = s.value(QStringLiteral("accounts/ids")).toStringList();
  const QStringList names =
      s.value(QStringLiteral("accounts/names")).toStringList();
  const QString kDefault = QStringLiteral("__default__");

  // With winding down enabled, accounts start dormant — no page, no renderer, no
  // download — and the one that ends up active is built by setActiveAccount()
  // below. The rest come into existence when the user first clicks them, which is
  // the same bargain the setting already makes for accounts that go idle later.
  // Off (the default), every account is built at startup exactly as before.
  const bool load = !Performance::suspendInactiveAccounts();

  if (ids.isEmpty()) {
    // Fresh install: just the default account.
    addAccount(QString(), tr("Account 1"), load);
  } else if (!ids.contains(kDefault)) {
    // Legacy save (before order-with-default): default implicit and first, then
    // the saved non-default accounts in order.
    addAccount(QString(), tr("Account 1"), load);
    for (int i = 0; i < ids.size(); ++i)
      addAccount(ids[i], names.value(i, tr("Account %1").arg(i + 2)), load);
  } else {
    // Recreate the saved order exactly, including where the default sits.
    for (int i = 0; i < ids.size(); ++i) {
      const QString id = (ids[i] == kDefault) ? QString() : ids[i];
      addAccount(id, names.value(i, tr("Account %1").arg(i + 1)), load);
    }
  }

  refreshAccountTabs();
  // Reopen on whichever account was active when the app last closed. Falls back
  // to the first tab when nothing is stored yet, or when that account has since
  // been removed.
  int active = 0;
  const QString wantedActive = s.value(QStringLiteral("accounts/active")).toString();
  if (!wantedActive.isEmpty()) {
    const int found =
        accountIndexForId(wantedActive == kDefault ? QString() : wantedActive);
    if (found >= 0)
      active = found;
  }
  setActiveAccount(active);
}
