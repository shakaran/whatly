// In-window accounts: a tab bar over a stack of WhatsApp views, one per signed-
// in account. The tab bar hides itself when only the default account exists, so
// a single-account setup is untouched by any of this.
#include "mainwindow.h"

#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSizeGrip>
#include <QLabel>
#include <QSet>
#include <QWidget>
#include <QtMath>

#include "settingsmanager.h"
#include "customtitlebar.h"
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
#include "webview.h"

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

void MainWindow::buildAccountArea() {
  m_focusOrder.append(nullptr); // the main window starts as the focused ("main") one

  // Debounced layout save: window moves/resizes fire rapidly during a drag, so
  // coalesce them into a single write after the motion settles.
  m_layoutSaveTimer = new QTimer(this);
  m_layoutSaveTimer->setSingleShot(true);
  m_layoutSaveTimer->setInterval(500);
  connect(m_layoutSaveTimer, &QTimer::timeout, this,
          [this]() { saveWindowLayout(); });
  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Optional client-side title bar (frameless mode). Sits above everything.
  if (CustomTitleBar::isEnabled())
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
  m_gridContainer = new QWidget(central);
  auto *grid = new QGridLayout(m_gridContainer);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(2);

  m_displayStack = new QStackedWidget(central);
  m_displayStack->setSizePolicy(expanding);
  m_displayStack->addWidget(m_accountStack);   // page 0: tabs
  m_displayStack->addWidget(m_gridContainer);  // page 1: grid

  layout->addWidget(m_accountBar);
  layout->addWidget(m_displayStack);

  // In frameless mode there is no native resize edge, so give the window a
  // corner size grip to drag.
  if (CustomTitleBar::isEnabled()) {
    auto *gripRow = new QHBoxLayout;
    gripRow->setContentsMargins(0, 0, 0, 0);
    gripRow->addStretch();
    gripRow->addWidget(new QSizeGrip(central), 0, Qt::AlignBottom | Qt::AlignRight);
    layout->addLayout(gripRow);
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
      m_settingsAction,    m_aboutAction,     m_viewTabsAction,
      m_viewGridAction,    m_quitAction};
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
  palette->exec();
}

// Tear the grid down, first rescuing the account views (which are owned by the
// app, not by the cell wrappers) so deleting a wrapper never deletes a view.
void MainWindow::clearGridCells() {
  auto *grid = m_gridContainer
                   ? qobject_cast<QGridLayout *>(m_gridContainer->layout())
                   : nullptr;
  // Rescue every account view that currently lives in a grid cell, so deleting
  // the cell wrappers never deletes a view.
  for (const Account &account : m_accounts)
    if (account.view && m_gridContainer &&
        m_gridContainer->isAncestorOf(account.view))
      account.view->setParent(nullptr);
  if (grid) {
    while (QLayoutItem *item = grid->takeAt(0)) {
      if (item->widget())
        item->widget()->deleteLater();
      delete item;
    }
  }
  m_gridLabels.clear();
}

void MainWindow::relayoutGrid() {
  if (!m_gridContainer)
    return;
  auto *grid = qobject_cast<QGridLayout *>(m_gridContainer->layout());
  if (!grid)
    return;
  clearGridCells();

  const int n = m_accounts.size();
  if (n == 0)
    return;

  // Grid shows EVERY account at once, wherever it normally lives — its view is
  // pulled out of its window into a tile, and handed back when Grid is left.
  const int cols = qMax(1, static_cast<int>(qCeil(qSqrt(qreal(n)))));
  for (int i = 0; i < n; ++i) {
    WebView *view = m_accounts[i].view;
    if (!view) {
      m_gridLabels.append(QPointer<QLabel>(nullptr));
      continue;
    }
    // Each cell is a caption (account name + unread) above its account view, so
    // it is obvious which tile is which.
    auto *cell = new QWidget(m_gridContainer);
    auto *box = new QVBoxLayout(cell);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    auto *caption = new QLabel(cell);
    caption->setObjectName(QStringLiteral("gridCellCaption"));
    caption->setAlignment(Qt::AlignCenter);
    caption->setContentsMargins(4, 2, 4, 2);
    // The caption labels its tile for assistive tech, and the view itself gets
    // the account name so focus announcements are meaningful.
    view->setAccessibleName(m_accounts[i].name);
    box->addWidget(caption);
    box->addWidget(view, 1); // reparents the view into the cell, from anywhere
    m_gridLabels.append(caption);
    grid->addWidget(cell, i / cols, i % cols);
    view->show();
  }
  updateGridCaptions();
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
    // Grow the window so each tile is at least the WebApp's usable minimum,
    // remembering the current size to restore when Grid is left. When the window
    // is already big enough the grid layout just divides the space equally. A
    // maximised / full-screen window already has plenty of room.
    if (!isMaximized() && !isFullScreen()) {
      m_preGridGeometry = geometry();
      const int n = qMax(1, m_accounts.size());
      const int cols = qMax(1, static_cast<int>(qCeil(qSqrt(qreal(n)))));
      const int rows = (n + cols - 1) / cols;
      resize(qMax(width(), cols * kBaseMinWidth),
             qMax(height(), rows * (kBaseMinHeight + 30)));
    }
    // Collapse everything: hide the strip and the detached windows, and pull
    // every account's view into the tiles.
    m_accountBar->hide();
    for (DetachedAccountWindow *w : wins)
      w->hide();
    relayoutGrid(); // tiles ALL accounts, reparenting their views into cells
    m_displayStack->setCurrentWidget(m_gridContainer);
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

  m_accountStack->addWidget(view);
  m_accounts.append({id, name, view, 0});

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
  m_activeAccount = index;
  m_webEngine = m_accounts[index].view;   // everything current-account flows through this
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
  // Re-point the lock overlay and refresh the title to the now-active account.
  if (m_webEngine && m_webEngine->page())
    setWindowTitle(QApplication::applicationDisplayName() + AppProfile::label() +
                   ": " + m_webEngine->page()->title());
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
    m_accountBar->setTabToolTip(t, QString());
    if (i == m_activeAccount)
      activeTab = t;
  }
  const int plus = docked.size();
  m_accountBar->setTabText(plus, QStringLiteral("  +  "));
  m_accountBar->setTabData(plus, QVariant()); // no data marks the "+" tab
  m_accountBar->setTabToolTip(plus, tr("Add another account"));

  // Strip shows whenever ≥2 accounts exist app-wide (every window gets one).
  m_accountBar->setVisible(m_accounts.size() > 1);
  m_accountBar->setCurrentIndex(activeTab);

  refreshDetachedStrips(); // keep every detached window's strip in step too
}

void MainWindow::updateTrayUnread() {
  int total = 0;
  for (const Account &a : m_accounts)
    total += a.unread;

  if (QFile f(unreadCountFile());
      f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QByteArray::number(total));
    f.close();
  }

  if (total > 0) {
    m_restoreAction->setText(tr("Restore") + " | " + QString::number(total) +
                             " " + (total > 1 ? tr("messages") : tr("message")));
    m_systemTrayIcon->setIcon(getTrayIcon(total));
    setWindowIcon(getTrayIcon(total));
  } else {
    m_restoreAction->setText(tr("Restore"));
    m_systemTrayIcon->setIcon(m_trayIconNormal);
    setWindowIcon(themeIcon("whatly", ":/icons/app/icon-64.png"));
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

void MainWindow::promptAddAccount() {
  // Put the strip back on the active account: the click landed on "+", which is
  // not a real page.
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
  // The new account joins the focused ("main") window — which may be a detached
  // window if that is where the user was last working.
  DetachedAccountWindow *focused =
      m_focusOrder.isEmpty() ? nullptr : m_focusOrder.first();
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
    if (!d.isValid())
      return;
    const int idx = accountIndexForId(d.toString());
    if (idx >= 0 && m_accounts[idx].view) {
      win->stack()->setCurrentWidget(m_accounts[idx].view);
      win->setWindowTitle(m_accounts[idx].name + QStringLiteral(" — ") +
                          QApplication::applicationDisplayName());
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
  // Moving/resizing the window updates the saved arrangement (debounced).
  connect(win, &DetachedAccountWindow::geometryChanged, this, [this]() {
    if (m_layoutSaveTimer)
      m_layoutSaveTimer->start();
  });
  // Closing the window docks its accounts back into the front-most survivor.
  connect(win, &DetachedAccountWindow::closed, this,
          [this, win]() { closeDetachedWindow(win); });
  return win;
}

void MainWindow::noteWindowFocused(DetachedAccountWindow *win) {
  m_focusOrder.removeAll(win);
  m_focusOrder.prepend(win); // most-recently-focused first; front is "main"
}

void MainWindow::destroyDetachedWindow(DetachedAccountWindow *win) {
  if (!win)
    return;
  m_focusOrder.removeAll(win);
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
    targetWin->setWindowTitle(m_accounts[mi].name + QStringLiteral(" — ") +
                              QApplication::applicationDisplayName());
  targetWin->show();
  targetWin->raise();
  targetWin->activateWindow();
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
    while (bar->count() > members.size())
      bar->removeTab(bar->count() - 1);
    while (bar->count() < members.size())
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
      if (m_accounts[i].view == current)
        activeTab = t;
    }
    if (!members.isEmpty()) {
      bar->setCurrentIndex(activeTab);
      const int i = members[qBound(0, activeTab, members.size() - 1)];
      win->stack()->setCurrentWidget(m_accounts[i].view);
      win->setWindowTitle(m_accounts[i].name + QStringLiteral(" — ") +
                          QApplication::applicationDisplayName());
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
  s.endGroup();

  const bool remember =
      s.value(QStringLiteral("rememberWindowLayout"), false).toBool();

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

  if (ids.isEmpty()) {
    // Fresh install: just the default account.
    addAccount(QString(), tr("Account 1"), true);
  } else if (!ids.contains(kDefault)) {
    // Legacy save (before order-with-default): default implicit and first, then
    // the saved non-default accounts in order.
    addAccount(QString(), tr("Account 1"), true);
    for (int i = 0; i < ids.size(); ++i)
      addAccount(ids[i], names.value(i, tr("Account %1").arg(i + 2)), true);
  } else {
    // Recreate the saved order exactly, including where the default sits.
    for (int i = 0; i < ids.size(); ++i) {
      const QString id = (ids[i] == kDefault) ? QString() : ids[i];
      addAccount(id, names.value(i, tr("Account %1").arg(i + 1)), true);
    }
  }

  refreshAccountTabs();
  setActiveAccount(0);
}
