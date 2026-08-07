#include "accounttabbar.h"

#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>

// The dragged account's id travels as the mime payload; the distinct type lets
// a strip recognise an account-tab drag (single process, so no cross-app risk).
static const QString kAccountTabMime =
    QStringLiteral("application/x-whatly-account-tab");

AccountTabBar::AccountTabBar(QWidget *parent) : QTabBar(parent) {
  // Live within-strip reordering: the tabs slide as you drag. Leaving the strip
  // vertically hands off to a QDrag (tear-off / cross-window) in mouseMove.
  setMovable(true);
}

int AccountTabBar::accountTabCount() const {
  int n = 0;
  for (int i = 0; i < count(); ++i)
    if (tabData(i).isValid()) // the "+" affordance has no data
      ++n;
  return n;
}

int AccountTabBar::dropSlotAt(int x) const {
  const int n = accountTabCount();
  for (int i = 0; i < n; ++i)
    if (x < tabRect(i).center().x())
      return i;
  return n;
}

int AccountTabBar::indexOfAccount(const QString &id) const {
  if (id.isEmpty())
    return -1;
  for (int i = 0; i < count(); ++i)
    if (tabData(i).toString() == id)
      return i;
  return -1;
}

void AccountTabBar::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Remember the account, not the slot. An empty id covers both "the + tab"
    // (no tab data) and "no tab here at all", which is what the slot number used
    // to be checked for.
    m_pressId = tabData(tabAt(event->position().toPoint())).toString();
    m_pressPos = event->position().toPoint();
  }
  QTabBar::mousePressEvent(event);
}

void AccountTabBar::mouseMoveEvent(QMouseEvent *event) {
  // While the cursor stays within the strip, QTabBar reorders tabs live. Once
  // it leaves the strip vertically, hand off to a QDrag that tears the tab off
  // or docks it into another window.
  if (!m_pressId.isEmpty() && (event->buttons() & Qt::LeftButton)) {
    const QPoint p = event->position().toPoint();
    if (p.y() < -kDetachMargin || p.y() > height() + kDetachMargin) {
      const QString id = m_pressId;
      m_pressId.clear();
      // End QTabBar's in-progress move before starting the drag.
      QMouseEvent release(QEvent::MouseButtonRelease, event->position(),
                          event->globalPosition(), Qt::LeftButton,
                          Qt::NoButton, event->modifiers());
      QTabBar::mouseReleaseEvent(&release);
      // Only now ask where that account sits. Taking the slot from the press
      // instead was the bug: drag a tab out on a path that slid it past its
      // neighbours first, and the slot pressed had come to hold one of THEM, so
      // the wrong account was torn into the new window. Resolving after the
      // release also means the tabs have settled into the order on screen.
      const int index = indexOfAccount(id);
      if (index >= 0)
        startDrag(index);
      return;
    }
  }
  QTabBar::mouseMoveEvent(event);
}

void AccountTabBar::startDrag(int index) {
  const QString id = tabData(index).toString();
  const QRect r = tabRect(index);
  const QPixmap sprite = grab(r); // the tab's own pixels, as the drag cursor

  auto *drag = new QDrag(this);
  auto *mime = new QMimeData;
  mime->setData(kAccountTabMime, id.toUtf8());
  drag->setMimeData(mime);
  drag->setPixmap(sprite);
  drag->setHotSpot(m_pressPos - r.topLeft());

  // Over the desktop or another app nothing accepts our private mime, so the
  // platform would show the "forbidden" cursor — misleading, since we always
  // complete the move on release. Give every action the same small badge cursor
  // so the pointer consistently reads as "drop to place here".
  QPixmap badge(22, 22);
  badge.fill(Qt::transparent);
  {
    QPainter bp(&badge);
    bp.setRenderHint(QPainter::Antialiasing);
    bp.setPen(Qt::NoPen);
    bp.setBrush(QColor(0x25, 0xD3, 0x66)); // WhatsApp green
    bp.drawEllipse(1, 1, 20, 20);
    bp.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap));
    bp.drawLine(11, 6, 11, 16);
    bp.drawLine(6, 11, 16, 11);
  }
  for (Qt::DropAction a :
       {Qt::MoveAction, Qt::CopyAction, Qt::LinkAction, Qt::IgnoreAction})
    drag->setDragCursor(badge, a);

  // Always report where the drag ended; the receiver decides what it landed on
  // (a strip that already handled it, another window's body, or empty space).
  // exec()'s action is unreliable here: a QWebEngineView under the cursor may
  // "accept" the drop for its own file-drop zone yet do nothing with our mime.
  drag->exec(Qt::MoveAction);
  emit dragReleased(id, QCursor::pos());
}

void AccountTabBar::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat(kAccountTabMime)) {
    m_dropSlot = dropSlotAt(event->position().toPoint().x());
    update();
    event->acceptProposedAction();
  } else {
    QTabBar::dragEnterEvent(event);
  }
}

void AccountTabBar::dragMoveEvent(QDragMoveEvent *event) {
  if (event->mimeData()->hasFormat(kAccountTabMime)) {
    const int slot = dropSlotAt(event->position().toPoint().x());
    if (slot != m_dropSlot) {
      m_dropSlot = slot;
      update();
    }
    event->acceptProposedAction();
  } else {
    QTabBar::dragMoveEvent(event);
  }
}

void AccountTabBar::dragLeaveEvent(QDragLeaveEvent *event) {
  m_dropSlot = -1;
  update();
  QTabBar::dragLeaveEvent(event);
}

void AccountTabBar::dropEvent(QDropEvent *event) {
  if (event->mimeData()->hasFormat(kAccountTabMime)) {
    const QString id = QString::fromUtf8(event->mimeData()->data(kAccountTabMime));
    const int slot =
        m_dropSlot >= 0 ? m_dropSlot : dropSlotAt(event->position().toPoint().x());
    m_dropSlot = -1;
    update();
    event->acceptProposedAction();
    emit accountDropped(id, slot);
  } else {
    QTabBar::dropEvent(event);
  }
}

void AccountTabBar::paintEvent(QPaintEvent *event) {
  QTabBar::paintEvent(event);
  if (m_dropSlot < 0)
    return;

  const int n = accountTabCount();
  int x;
  if (n == 0)
    x = 0;
  else if (m_dropSlot >= n)
    x = tabRect(n - 1).right();
  else
    x = tabRect(m_dropSlot).left();
  const int top = n > 0 ? tabRect(0).top() : 0;

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor c = palette().highlight().color();
  p.setPen(QPen(c, 2));
  p.drawLine(x, top, x, top + height());
  // A small ▾ at the top of the insertion boundary, like Chrome.
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  QPolygon tri;
  tri << QPoint(x - 4, top) << QPoint(x + 4, top) << QPoint(x, top + 5);
  p.drawPolygon(tri);
}
