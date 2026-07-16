#ifndef GLOBALSHORTCUT_H
#define GLOBALSHORTCUT_H

#include <QAbstractNativeEventFilter>
#include <QObject>

// A system-wide hotkey to raise the window (Ctrl+Alt+W).
//
// X11 only. On Wayland the compositor deliberately forbids ordinary apps from
// grabbing global keys, so tryRegister() returns false there — bind a desktop
// shortcut to `whatly -w` instead (that path works on every session type).
// activated() fires whenever the combo is pressed, regardless of focus.
class GlobalShortcut : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT
public:
  explicit GlobalShortcut(QObject *parent = nullptr);
  ~GlobalShortcut() override;

  // Grab Ctrl+Alt+W. Returns true on success — false on Wayland, without an X11
  // display, or when the combination is already taken by something else.
  bool tryRegister();

  bool nativeEventFilter(const QByteArray &eventType, void *message,
                         qintptr *result) override;

signals:
  void activated();

private:
  void ungrab();
  quint32 m_keycode = 0;   // X keycode of 'W'
  quint32 m_modifiers = 0; // X modifier mask (Control + Alt)
  bool m_registered = false;
};

#endif // GLOBALSHORTCUT_H
