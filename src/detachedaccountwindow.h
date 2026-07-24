#ifndef DETACHEDACCOUNTWINDOW_H
#define DETACHEDACCOUNTWINDOW_H

#include <QWidget>

class AccountTabBar;
class QStackedWidget;

// A top-level peer window that can host one OR MORE accounts, with its own tab
// strip over a stack of their views. It owns no account data: MainWindow (the
// single coordinator) reparents account views into stack(), rebuilds bar()'s
// tabs, and moves accounts between windows. The views are only borrowed here —
// closing the window must never delete them, so closeEvent just tells
// MainWindow (via closed()) to dock the accounts back.
class DetachedAccountWindow : public QWidget {
  Q_OBJECT
public:
  explicit DetachedAccountWindow(QWidget *parent = nullptr);

  AccountTabBar *bar() const { return m_bar; }
  QStackedWidget *stack() const { return m_stack; }

signals:
  // Emitted once when the window closes, so MainWindow docks its accounts back.
  void closed();
  // Emitted when this window becomes the active (focused) window, so MainWindow
  // can keep its most-recently-focused ("main") ordering.
  void activated();
  // Emitted when the window is moved or resized, so MainWindow can persist the
  // updated arrangement (debounced on its side).
  void geometryChanged();

protected:
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  AccountTabBar *m_bar = nullptr;
  QStackedWidget *m_stack = nullptr;
  bool m_closeEmitted = false;
};

#endif // DETACHEDACCOUNTWINDOW_H
