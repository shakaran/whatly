#ifndef CUSTOMTITLEBAR_H
#define CUSTOMTITLEBAR_H

#include <QWidget>

class QLabel;
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
  // window title. Merged drops the icon and shrinks the title to a watermark,
  // so it can sit at the right-hand end of the account tab strip and let the
  // tabs themselves be the title bar — Chrome-style, one row instead of two.
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
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // The window's title with the application's name in front of it. Window titles
  // themselves no longer carry the name, because Qt appends it to every one of
  // them for the system's title bar, the task list and Alt-Tab; a bar we draw
  // has no such platform behind it, so it puts the name back itself.
  QString barTitle() const;
  void toggleMaximized();
  void refreshMaximizeIcon();

  QWidget *m_window = nullptr;
  QLabel *m_icon = nullptr;
  // The window title. Its own row in a standalone bar, beside the app icon; in
  // the run of strip the tabs leave in a merged one, small and faint, because
  // there it is sharing a row that has a job already.
  QLabel *m_title = nullptr;
  QToolButton *m_maxButton = nullptr;
};

#endif // CUSTOMTITLEBAR_H
