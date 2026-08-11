#ifndef LINGERTIP_H
#define LINGERTIP_H

#include <QObject>
#include <QPoint>
#include <QString>

#include <functional>

class QTimer;
class QWidget;

// A tooltip that waits for the pointer to stop. Qt's own appears after a
// fraction of a second, which is right for a control with something to explain
// and wrong for a run of chrome someone is only crossing: what this answers is
// not urgent, and a tip that flashed up every time a hand passed over the tabs
// would be worse than no answer at all.
//
// Five seconds of stillness, put back to the beginning by every move, and gone
// again the moment the pointer moves.
class LingerTip : public QObject {
  Q_OBJECT
public:
  // Watch `widget` and show `text` over it. `eligible` says which of its points
  // answer at all, so the parts that have tooltips of their own — the tabs, the
  // window buttons — keep them. The instance is owned by the widget.
  static void install(QWidget *widget, QString text,
                      std::function<bool(const QPoint &)> eligible = {});

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  LingerTip(QWidget *widget, QString text,
            std::function<bool(const QPoint &)> eligible);
  // Whether a point of the widget answers, given the caller's rule.
  bool eligibleAt(const QPoint &pos) const;

  QWidget *m_widget = nullptr;
  QString m_text;
  std::function<bool(const QPoint &)> m_eligible;
  QTimer *m_timer = nullptr;
};

#endif // LINGERTIP_H
