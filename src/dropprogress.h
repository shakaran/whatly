#ifndef DROPPROGRESS_H
#define DROPPROGRESS_H

#include <QPointer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;

// The little bar shown while a dropped file is being read, sitting over the
// bottom of the chat. Reading a video takes a few seconds, and without this the
// window simply sat there looking stuck.
//
// It parents itself to the top-level window rather than to the view it covers:
// a child of QWebEngineView has to fight the render widget for stacking order,
// while a sibling of it does not.
class DropProgress : public QWidget {
  Q_OBJECT

public:
  // host is the view the bar is placed over; it stays owned by host's window.
  explicit DropProgress(QWidget *host);

  // Show the bar at zero, ready for setProgress().
  void begin();
  void setProgress(qint64 bytesRead, qint64 bytesTotal);

  // Take the bar down. With a message, that is shown on its own for a few
  // seconds first (used to say which files were too large).
  void finish(const QString &message = QString());

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void reposition();

  // Guarded: the bar is parented to the top-level window, so it can outlive the
  // view it covers; a raw pointer would dangle. reposition() null-checks it.
  QPointer<QWidget> m_host;
  QLabel *m_label;
  QProgressBar *m_bar;
  QTimer *m_showTimer; // delays the panel so a fast drop never flashes it
  QTimer *m_hideTimer; // takes a closing message down; cancelled by begin()
};

#endif // DROPPROGRESS_H
