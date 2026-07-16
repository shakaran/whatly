#include "globalshortcut.h"

#include <QGuiApplication>

#if defined(Q_OS_LINUX)
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
// Xlib leaks a pile of unprefixed macros that collide with Qt/xcb identifiers.
// We only need the ones used below, so drop the troublesome ones right away.
#undef Bool
#undef None
#undef Status
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef Always

namespace {
Display *x11Display() {
  if (auto *x = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
    return x->display();
  return nullptr;
}
// CapsLock (LockMask) and NumLock (Mod2Mask) change the event state, so the
// grab has to cover every combination or the shortcut stops firing while they
// are on.
const unsigned int kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
int ignoreXError(Display *, XErrorEvent *) { return 0; }
} // namespace
#endif

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}

GlobalShortcut::~GlobalShortcut() { ungrab(); }

bool GlobalShortcut::tryRegister() {
#if defined(Q_OS_LINUX)
  if (QGuiApplication::platformName() != QLatin1String("xcb"))
    return false; // Wayland and the rest: not supported by design
  Display *dpy = x11Display();
  if (!dpy)
    return false;
  m_keycode = XKeysymToKeycode(dpy, XK_w);
  if (m_keycode == 0)
    return false;
  m_modifiers = ControlMask | Mod1Mask; // Ctrl + Alt
  const Window root = DefaultRootWindow(dpy);
  XErrorHandler prev = XSetErrorHandler(ignoreXError);
  for (unsigned int lock : kLockMasks)
    XGrabKey(dpy, m_keycode, m_modifiers | lock, root, 1 /*owner_events*/,
             GrabModeAsync, GrabModeAsync);
  XSync(dpy, 0);
  XSetErrorHandler(prev);
  qApp->installNativeEventFilter(this);
  m_registered = true;
  return true;
#else
  return false;
#endif
}

void GlobalShortcut::ungrab() {
#if defined(Q_OS_LINUX)
  if (!m_registered)
    return;
  if (Display *dpy = x11Display()) {
    const Window root = DefaultRootWindow(dpy);
    for (unsigned int lock : kLockMasks)
      XUngrabKey(dpy, m_keycode, m_modifiers | lock, root);
    XSync(dpy, 0);
  }
  qApp->removeNativeEventFilter(this);
  m_registered = false;
#endif
}

bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType,
                                       void *message, qintptr *) {
#if defined(Q_OS_LINUX)
  if (!m_registered || eventType != QByteArrayLiteral("xcb_generic_event_t"))
    return false;
  auto *ev = static_cast<xcb_generic_event_t *>(message);
  if ((ev->response_type & ~0x80) == XCB_KEY_PRESS) {
    auto *ke = reinterpret_cast<xcb_key_press_event_t *>(ev);
    // Match on Ctrl+Alt only; ignore the lock modifiers we grabbed for.
    const unsigned int mask = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;
    if (ke->detail == m_keycode && (ke->state & mask) == m_modifiers)
      emit activated();
  }
#else
  Q_UNUSED(eventType);
  Q_UNUSED(message);
#endif
  return false;
}
