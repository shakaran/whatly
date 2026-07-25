#include "detachedaccountwindow.h"
#include "accounttabbar.h"

#include <QCloseEvent>
#include <QEvent>
#include <QIcon>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

DetachedAccountWindow::DetachedAccountWindow(QWidget *parent) : QWidget(parent) {
  // A parentless top-level so the window manager treats it as independent: it
  // stays visible when the main window is minimised to the tray, which is the
  // whole point of tearing accounts off to watch them on their own.
  setWindowFlag(Qt::Window, true);
  setWindowIcon(QIcon(QStringLiteral(":/icons/app/icon-64.png")));
  resize(900, 700);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Its own tab strip — a first-class peer of the main window. It accepts
  // dropped tabs, so accounts can be dragged into this window from others.
  m_bar = new AccountTabBar(this);
  m_bar->setObjectName("accountBar");
  m_bar->setExpanding(false);
  m_bar->setDrawBase(false);
  m_bar->setFocusPolicy(Qt::NoFocus);
  m_bar->setAcceptDrops(true);
  m_bar->setContextMenuPolicy(Qt::CustomContextMenu); // MainWindow builds the menu
  layout->addWidget(m_bar);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack, 1);
}

void DetachedAccountWindow::closeEvent(QCloseEvent *event) {
  // Hand the accounts back to the main window rather than letting their views
  // die with this widget. Guarded so a second close (or teardown) is a no-op.
  if (!m_closeEmitted) {
    m_closeEmitted = true;
    emit closed();
  }
  event->accept();
}

void DetachedAccountWindow::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ActivationChange && isActiveWindow())
    emit activated();
  QWidget::changeEvent(event);
}

void DetachedAccountWindow::moveEvent(QMoveEvent *event) {
  QWidget::moveEvent(event);
  emit geometryChanged();
}

void DetachedAccountWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  emit geometryChanged();
}
