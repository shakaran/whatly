#ifndef ACCOUNTTABBAR_H
#define ACCOUNTTABBAR_H

#include <QPixmap>
#include <QPoint>
#include <QString>
#include <QTabBar>
#include <QVariant>

class QMouseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QPaintEvent;

// Account tab strip with Chrome-style dragging. Each account tab stores its
// stable account id in tab data (a QString; the "+" affordance has no data, so
// an invalid QVariant marks it). Dragging lifts a sprite of the tab that
// follows the cursor:
//   * dropped onto a strip that accepts drops -> accountDropped() at a slot
//     shown live by a ▾ insertion marker.
//   * every drag also emits dragReleased() with the end position, so the owner
//     can route drops that missed a strip (onto a window body, or empty space).
// A strip is a drop target only when its owner calls setAcceptDrops(true).
class AccountTabBar : public QTabBar {
  Q_OBJECT
public:
  explicit AccountTabBar(QWidget *parent = nullptr);

  // Which slot currently holds the account `id`, or -1. Public because it is the
  // whole point of storing the id: a tab's slot is not stable for as long as a
  // drag lasts, so anything that has to name a tab later must ask this rather
  // than remember a number.
  int indexOfAccount(const QString &id) const;

signals:
  // The drag for account `id` ended at `globalPos`. Fired for EVERY drag,
  // whatever exec() reported — a QWebEngineView under the cursor may "accept"
  // the drop yet ignore our mime, so the receiver decides by geometry.
  void dragReleased(const QString &id, const QPoint &globalPos);
  // The tab for account `id` was dropped onto this strip; it should become the
  // account at position `insertIndex` among this strip's account tabs.
  void accountDropped(const QString &id, int insertIndex);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  // Re-tints on a palette change, so switching light/dark theme is followed.
  void changeEvent(QEvent *event) override;

private:
  // Tint the tabs that are NOT on screen, derived from the current palette:
  // lighter than the strip in a dark theme, darker in a light one. Some platform
  // style and GTK theme combinations draw the selected tab almost identically to
  // the rest, which on a strip that doubles as the window's title bar leaves
  // nothing to say which account is on screen. Tinting the crowd rather than the
  // one answers that the same way round, and leaves the selected tab as the
  // platform draws it — one flat colour and one odd one out.
  void refreshSelectionTint();
  // Guards refreshSelectionTint against re-entering itself: the setStyleSheet it
  // does makes Qt re-resolve the style, which sends events that come back here.
  bool m_tinting = false;

private:
  // `cursorPos` is where the pointer was when the drag began, in strip
  // coordinates; it sets where the sprite hangs off the cursor.
  void startDrag(int index, const QPoint &cursorPos);
  int accountTabCount() const;   // tabs backing a real account (valid tab data)
  int dropSlotAt(int x) const;   // insertion slot for a cursor x position

  // How far outside the strip (px) the cursor must go before a within-strip
  // reorder becomes a tear-off / cross-window drag.
  static constexpr int kDetachMargin = 24;
  // The account pressed, not the slot it was pressed in. QTabBar reorders tabs
  // live under the cursor while the button is down, so a slot number captured on
  // press stops meaning that account the moment the drag moves sideways.
  //
  // Held as the tab data itself rather than as a QString, because the DEFAULT
  // account's id is the empty string and a bare id cannot tell it apart from
  // "there is no account here". Validity is the question being asked; the "+"
  // affordance is the one carrying no data at all.
  QVariant m_pressData;
  // The tab's pixels, taken at press time. Grabbing them when the drag starts
  // catches QTabBar's reorder animation in flight, which draws halves of two
  // different tabs into the sprite; at press the strip is stationary.
  QPixmap m_pressSprite;
  int m_dropSlot = -1; // insertion slot to paint while a drag hovers, or -1
};

#endif // ACCOUNTTABBAR_H
