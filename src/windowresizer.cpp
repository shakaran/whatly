#include "windowresizer.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace {

// The cursor that says what a drag from here would do.
Qt::CursorShape cursorFor(Qt::Edges edges) {
  const bool left = edges.testFlag(Qt::LeftEdge);
  const bool right = edges.testFlag(Qt::RightEdge);
  const bool top = edges.testFlag(Qt::TopEdge);
  const bool bottom = edges.testFlag(Qt::BottomEdge);
  if ((left && top) || (right && bottom))
    return Qt::SizeFDiagCursor;
  if ((right && top) || (left && bottom))
    return Qt::SizeBDiagCursor;
  if (left || right)
    return Qt::SizeHorCursor;
  if (top || bottom)
    return Qt::SizeVerCursor;
  return Qt::ArrowCursor;
}

} // namespace

WindowResizer::WindowResizer(QWidget *frame, QWidget *window)
    : QObject(frame), m_frame(frame), m_window(window) {}

void WindowResizer::install(QWidget *frame, QWidget *window) {
  if (!frame || !window)
    return;
  auto *resizer = new WindowResizer(frame, window);
  // Mouse tracking so the cursor changes on the way in, without a button held.
  frame->setMouseTracking(true);
  frame->installEventFilter(resizer);
}

Qt::Edges WindowResizer::edgesAt(const QPoint &pos) const {
  Qt::Edges edges;
  if (!m_frame)
    return edges;
  const QRect r = m_frame->rect();
  if (pos.x() <= r.left() + kBorder)
    edges |= Qt::LeftEdge;
  else if (pos.x() >= r.right() - kBorder)
    edges |= Qt::RightEdge;
  if (pos.y() <= r.top() + kBorder)
    edges |= Qt::TopEdge;
  else if (pos.y() >= r.bottom() - kBorder)
    edges |= Qt::BottomEdge;
  return edges;
}

bool WindowResizer::eventFilter(QObject *watched, QEvent *event) {
  if (watched != m_frame || !m_window)
    return QObject::eventFilter(watched, event);

  switch (event->type()) {
  case QEvent::MouseMove: {
    auto *me = static_cast<QMouseEvent *>(event);
    // Only while no button is down: during a drag the compositor owns the
    // pointer, and re-reading the position here would fight it.
    if (me->buttons() == Qt::NoButton)
      m_frame->setCursor(cursorFor(edgesAt(me->position().toPoint())));
    break;
  }
  case QEvent::Leave:
    m_frame->unsetCursor();
    break;
  case QEvent::MouseButtonPress: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton)
      break;
    const Qt::Edges edges = edgesAt(me->position().toPoint());
    if (!edges)
      break;
    QWindow *handle = m_window->windowHandle();
    if (!handle)
      break;
    // Maximized windows are not resizable by dragging an edge, and asking the
    // compositor to try leaves the pointer grabbed with nothing happening.
    if (m_window->isMaximized() || m_window->isFullScreen())
      break;
    if (handle->startSystemResize(edges))
      return true; // the window manager has the drag; do not also deliver it
    break;
  }
  default:
    break;
  }
  return QObject::eventFilter(watched, event);
}
