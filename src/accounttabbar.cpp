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
#include <QEvent>
#include <QPalette>

// The dragged account's id travels as the mime payload; the distinct type lets
// a strip recognise an account-tab drag (single process, so no cross-app risk).
static const QString kAccountTabMime =
    QStringLiteral("application/x-whatly-account-tab");

AccountTabBar::AccountTabBar(QWidget *parent) : QTabBar(parent) {
  // Live within-strip reordering: the tabs slide as you drag. Leaving the strip
  // vertically hands off to a QDrag (tear-off / cross-window) in mouseMove.
  setMovable(true);
  refreshSelectionTint();
}

void AccountTabBar::refreshSelectionTint() {
  // setStyleSheet() below makes Qt re-resolve this widget's style, which sends it
  // a StyleChange and can send a PaletteChange too — both of which land back in
  // changeEvent() and would come straight back here. Without this guard that is
  // unbounded recursion, and it crashed the app on start-up rather than
  // misbehaving quietly, because the stack ran out inside Qt's own style
  // machinery. Re-entrancy has to be blocked here rather than by picking "safe"
  // event types: which events a style re-resolve sends is Qt's business, not ours.
  if (m_tinting)
    return;
  m_tinting = true;
  // Derived from the palette rather than hard-coded, so it follows whatever the
  // theme is doing and stays right in both light and dark: a step away from the
  // strip, lighter when the strip is dark and darker when it is light.
  const QColor base = palette().color(QPalette::Window);
  const bool dark = base.lightness() < 128;
  const QColor tint = dark ? base.lighter(190) : base.darker(125);
  // And it goes on the tabs that are NOT on screen. With it on the selected one
  // instead, that tab was the only one the stylesheet drew and the others kept
  // the platform's own bordered look — which read as the wrong way round, the
  // bordered ones looking live and the flat one looking like background. Now the
  // ones you are not in are the flat crowd and the odd one out is the account you
  // are in, which keeps the point of tinting anything: the selected tab stays
  // visible on the styles that would otherwise draw it much like the rest.
  // Naming one state also keeps Qt from taking over the whole strip's drawing.
  setStyleSheet(
      QStringLiteral("QTabBar::tab:!selected{background:%1;}").arg(tint.name()));
  m_tinting = false;
}

void AccountTabBar::changeEvent(QEvent *event) {
  QTabBar::changeEvent(event);
  // Only the palette, and not StyleChange: a StyleChange is what our own
  // setStyleSheet() causes, so acting on it would be chasing our own tail. The
  // palette is what the tint is actually derived from, so a theme switch is
  // followed, and the guard above covers the rest.
  if (event->type() == QEvent::PaletteChange)
    refreshSelectionTint();
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
  for (int i = 0; i < count(); ++i)
    // Validity first: the "+" affordance carries no tab data, and the default
    // account's id is the empty string, so comparing ids alone would either
    // match the affordance or refuse to find the default account.
    if (tabData(i).isValid() && tabData(i).toString() == id)
      return i;
  return -1;
}

void AccountTabBar::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Remember the account, not the slot. The tab data itself, not the id it
    // holds: the default account's id is "", so an id alone cannot be told from
    // "no account here" — and treating it as nothing left the one account most
    // people have as the one account that could not be torn off.
    const int pressed = tabAt(event->position().toPoint());
    m_pressedTab = pressed >= 0;
    m_pressData = tabData(pressed);
    // Take the sprite now, while the strip is still. By the time a drag starts,
    // QTabBar has slid the tabs around and is animating them, so a grab there
    // catches two tabs mid-swap and draws halves of both.
    m_pressSprite = pressed >= 0 ? grab(tabRect(pressed)) : QPixmap();
    // Where that tab was when it was pressed. The click is owed to this patch of
    // strip, not to whatever slides into it afterwards.
    m_pressRect = pressed >= 0 ? tabRect(pressed) : QRect();

    // A press is only half a click, and QTabBar switches accounts on it. That
    // put a whole other WhatsApp session on screen for a tab that was merely
    // being dragged out of the strip, or one the pointer had left again before
    // the button came up. The press still goes to QTabBar, whose reordering
    // machinery is driven by it, but the selection it makes is put quietly
    // back; mouseReleaseEvent makes the real one, when the click is whole.
    const int before = currentIndex();
    QSignalBlocker quiet(this);
    QTabBar::mousePressEvent(event);
    if (currentIndex() != before)
      setCurrentIndex(before);
    return;
  }
  QTabBar::mousePressEvent(event);
}

void AccountTabBar::mouseReleaseEvent(QMouseEvent *event) {
  const bool pressedTab = m_pressedTab;
  const QVariant pressData = m_pressData;
  m_pressedTab = false;
  QTabBar::mouseReleaseEvent(event);

  if (event->button() != Qt::LeftButton || !pressedTab)
    return;
  // The click has to end where it began, on the tab that was pressed and on the
  // account that tab held. The pointer never left that patch of strip — a move
  // that leaves it puts m_pressedTab down — so nothing can have slid into it,
  // but the account is checked all the same: it is what the click was about.
  const QPoint pos = event->position().toPoint();
  const int under = tabAt(pos);
  if (m_pressRect.contains(pos) && under >= 0 && tabData(under) == pressData)
    setCurrentIndex(under);
}

void AccountTabBar::mouseMoveEvent(QMouseEvent *event) {
  // While the cursor stays within the strip, QTabBar reorders tabs live. Once
  // it leaves the strip vertically, hand off to a QDrag that tears the tab off
  // or docks it into another window.
  const QPoint p = event->position().toPoint();
  // Once the pointer leaves the tab it went down on, the click is over — and it
  // does not come back if the pointer does. Whatever this becomes now is a drag,
  // and a drag is not a choice: it is also what stops a within-strip reorder
  // from selecting the tab it has just finished moving.
  if (m_pressedTab && (event->buttons() & Qt::LeftButton) &&
      !m_pressRect.contains(p))
    m_pressedTab = false;

  if (m_pressData.isValid() && (event->buttons() & Qt::LeftButton)) {
    if (p.y() < -kDetachMargin || p.y() > height() + kDetachMargin) {
      const QString id = m_pressData.toString();
      m_pressData.clear();
      m_pressedTab = false; // this is a tear-off now, not a click on a tab
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
        startDrag(index, p);
      return;
    }
  }
  QTabBar::mouseMoveEvent(event);
}

void AccountTabBar::startDrag(int index, const QPoint &cursorPos) {
  const QString id = tabData(index).toString();
  const QRect r = tabRect(index);
  // The tab's own pixels, as the drag cursor — taken at press time, because
  // grabbing here would catch the reorder animation. Falling back to a live grab
  // only covers a drag that somehow began without a press.
  const QPixmap sprite = m_pressSprite.isNull() ? grab(r) : m_pressSprite;
  m_pressSprite = QPixmap();

  auto *drag = new QDrag(this);
  auto *mime = new QMimeData;
  mime->setData(kAccountTabMime, id.toUtf8());
  drag->setMimeData(mime);
  drag->setPixmap(sprite);
  // Hold the sprite where the cursor actually is. The press position cannot be
  // used for this any more: a drag that slid the tab past its neighbours on the
  // way out ends with the tab somewhere else entirely, so an offset measured from
  // where it was first grabbed leaves the sprite hanging at a distance from the
  // pointer. Vertically the cursor is by definition outside the strip — that is
  // what started the drag — so the grip goes to the middle of the tab.
  drag->setHotSpot(QPoint(qBound(0, cursorPos.x() - r.left(), r.width()),
                          r.height() / 2));

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
