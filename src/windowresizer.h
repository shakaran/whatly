#ifndef WINDOWRESIZER_H
#define WINDOWRESIZER_H

#include <QObject>
#include <Qt>

class QWidget;

// Gives a frameless top-level window the eight resize grab regions a normal
// window has: four edges and four corners.
//
// A frameless window has no native resize border, and Qt's answer in the
// codebase so far was a single QSizeGrip in the bottom-right corner — one corner
// out of eight. This restores the rest.
//
// The mechanism is QWindow::startSystemResize(), which hands the drag straight to
// the window manager. That matters: the resize then feels exactly like a normal
// window's on X11, Wayland and Windows alike, with the compositor doing the
// tracking, rather than us following the mouse and fighting it.
//
// The catch it has to work around is that the grab region needs mouse events, and
// a window whose child widgets cover every pixel never sees one — the web view
// swallows them. So the border is a real margin on the frame widget's layout,
// belonging to the frame itself rather than to any child, and that is the strip
// watched here.
class WindowResizer : public QObject {
  Q_OBJECT
public:
  // Width of the grab border, in device-independent pixels. Wide enough to hit
  // without aiming, narrow enough not to look like a frame.
  static constexpr int kBorder = 5;

  // Watch `frame` and resize its top-level `window`. `frame` must carry a
  // contents margin of at least kBorder on the sides that should be grabbable,
  // or there will be no strip to hit. Takes ownership via the QObject tree.
  static void install(QWidget *frame, QWidget *window);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  WindowResizer(QWidget *frame, QWidget *window);
  // Which edges the point is on, as a set: two bits for a corner, one for an
  // edge, none for anywhere else.
  Qt::Edges edgesAt(const QPoint &pos) const;

  QWidget *m_frame = nullptr;
  QWidget *m_window = nullptr;
};

#endif // WINDOWRESIZER_H
