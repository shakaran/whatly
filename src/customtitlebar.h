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
  // `window` is the top-level window this bar decorates.
  explicit CustomTitleBar(QWidget *window, QWidget *parent = nullptr);

  // Whether the custom-frame mode is enabled in settings.
  static bool isEnabled();
  static void setEnabled(bool enabled);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void toggleMaximized();
  void refreshMaximizeIcon();

  QWidget *m_window = nullptr;
  QLabel *m_icon = nullptr;
  QLabel *m_title = nullptr;
  QToolButton *m_maxButton = nullptr;
};

#endif // CUSTOMTITLEBAR_H
