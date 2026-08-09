#ifndef CUSTOMTITLEBAR_H
#define CUSTOMTITLEBAR_H

#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;

// A slim client-side title bar for the optional frameless-window mode. It is only
// created when the "custom window frame" setting is on (off by default, so the
// native decoration is untouched for everyone else). It carries the app icon and
// title, minimise / maximise / close buttons, drags the window with the
// compositor's own move grab (works on Wayland and X11), and toggles maximise on
// double-click.
class CustomTitleBar : public QWidget {
  Q_OBJECT
public:
  // Standalone is the bar described above: its own row, with the icon and the
  // window title. Merged drops both and keeps only the window buttons, so it
  // can sit at the right-hand end of the account tab strip and let the tabs
  // themselves be the title bar — Chrome-style, one row instead of two.
  enum class Mode { Standalone, Merged };

  // `window` is the top-level window this bar decorates.
  explicit CustomTitleBar(QWidget *window, QWidget *parent = nullptr,
                          Mode mode = Mode::Standalone);

  // Whether the custom-frame mode is enabled in settings.
  static bool isEnabled();
  static void setEnabled(bool enabled);

  // Whether the account tabs share the title bar's row. A refinement of the
  // custom frame rather than a mode of its own, so it is only ever true when
  // isEnabled() is.
  static bool tabsInTitleBar();
  static void setTabsInTitleBar(bool enabled);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void toggleMaximized();
  void refreshMaximizeIcon();

  QWidget *m_window = nullptr;
  QLabel *m_icon = nullptr;
  QLabel *m_title = nullptr;
  // The version, in the strip space the tabs leave in a merged bar. A standalone
  // one has a title to show instead, and shows only that: the version there was
  // a second thing in a row that already had a job, and it read as one.
  QLabel *m_version = nullptr;
  QToolButton *m_maxButton = nullptr;
  // Counts out the stillness a hover has to hold before the tooltip appears.
  QTimer *m_tipTimer = nullptr;
};

#endif // CUSTOMTITLEBAR_H
