#ifndef ACCOUNTTABBAR_H
#define ACCOUNTTABBAR_H

#include <QPoint>
#include <QString>
#include <QTabBar>

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
  void startDrag(int index);
  int accountTabCount() const;   // tabs backing a real account (valid tab data)
  int dropSlotAt(int x) const;   // insertion slot for a cursor x position

  // How far outside the strip (px) the cursor must go before a within-strip
  // reorder becomes a tear-off / cross-window drag.
  static constexpr int kDetachMargin = 24;
  int m_pressIndex = -1;
  QPoint m_pressPos;
  int m_dropSlot = -1; // insertion slot to paint while a drag hovers, or -1
};

#endif // ACCOUNTTABBAR_H
