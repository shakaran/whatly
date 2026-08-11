#include "detachedaccountwindow.h"
#include "accounttabbar.h"
#include "customtitlebar.h"
#include "windowresizer.h"

#include <QCloseEvent>
#include <QEvent>
#include <QIcon>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QVBoxLayout>

DetachedAccountWindow::DetachedAccountWindow(QWidget *parent) : QWidget(parent) {
  // A parentless top-level so the window manager treats it as independent: it
  // stays visible when the main window is minimised to the tray, which is the
  // whole point of tearing accounts off to watch them on their own.
  setWindowFlag(Qt::Window, true);
  setWindowIcon(QIcon(QStringLiteral(":/icons/app/icon-64.png")));
  resize(900, 700);

  // Same client-side decoration as the main window, for the same reason: a
  // detached account is a peer of it, not a lesser window, so with the custom
  // frame on it should not be the one thing still wearing the system's.
  const bool frameless = CustomTitleBar::isEnabled();
  if (frameless)
    setWindowFlag(Qt::FramelessWindowHint, true);

  auto *layout = new QVBoxLayout(this);
  // The margin is the resize border, and it exists only in frameless mode —
  // dropping the native frame is what takes the native resize edges with it.
  if (frameless)
    layout->setContentsMargins(WindowResizer::kBorder, WindowResizer::kBorder,
                               WindowResizer::kBorder, WindowResizer::kBorder);
  else
    layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // A standalone bar only when the tabs are not sharing the row; otherwise the
  // window buttons ride at the end of the strip, below.
  if (frameless && !CustomTitleBar::tabsInTitleBar())
    layout->addWidget(new CustomTitleBar(this, this));

  // Its own tab strip — a first-class peer of the main window. It accepts
  // dropped tabs, so accounts can be dragged into this window from others.
  m_bar = new AccountTabBar(this);
  m_bar->setObjectName("accountBar");
  m_bar->setExpanding(false);
  m_bar->setDrawBase(false);
  m_bar->setFocusPolicy(Qt::NoFocus);
  m_bar->setAcceptDrops(true);
  m_bar->setContextMenuPolicy(Qt::CustomContextMenu); // MainWindow builds the menu
  if (frameless && CustomTitleBar::tabsInTitleBar()) {
    // Chrome-style, mirroring the main window: the strip is the title bar, with
    // the window buttons at its right-hand end.
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(0);
    titleRow->addWidget(m_bar, 0);
    titleRow->addWidget(
        new CustomTitleBar(this, this, CustomTitleBar::Mode::Merged), 1);
    layout->addLayout(titleRow);
  } else {
    layout->addWidget(m_bar);
  }

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack, 1);

  // All eight resize regions, since dropping the native frame dropped the
  // native ones. Installed last, so the border margin is already in place.
  if (frameless)
    WindowResizer::install(this, this);
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
