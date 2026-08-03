#include "globalshortcut.h"

#include <QGuiApplication>

#if defined(Q_OS_LINUX)
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>

// ── XDG GlobalShortcuts portal marshalling ──────────────────────────────────
// BindShortcuts takes a(sa{sv}): a list of (id, metadata) structs. The type
// lives at file scope (not in an anonymous namespace) so Q_DECLARE_METATYPE can
// name it.
struct PortalShortcut {
  QString id;
  QVariantMap meta;
};
Q_DECLARE_METATYPE(PortalShortcut)

static QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s) {
  arg.beginStructure();
  arg << s.id << s.meta;
  arg.endStructure();
  return arg;
}
static const QDBusArgument &operator>>(const QDBusArgument &arg,
                                       PortalShortcut &s) {
  arg.beginStructure();
  arg >> s.id >> s.meta;
  arg.endStructure();
  return arg;
}

namespace {
constexpr char kPortalService[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kPortalIface[] = "org.freedesktop.portal.GlobalShortcuts";
} // namespace

// ── X11 fallback (raw key grab) ─────────────────────────────────────────────
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
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
const unsigned int kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
int ignoreXError(Display *, XErrorEvent *) { return 0; }
} // namespace
#endif // Q_OS_LINUX

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {
  // The system-wide shortcuts Whatly registers. Ctrl+Alt+W raises the window;
  // Ctrl+Alt+N opens the quick-compose box (idea #4) without raising it.
  Binding raise;
  raise.id = QStringLiteral("raise-window");
  raise.trigger = QStringLiteral("CTRL+ALT+w");
  raise.description = QStringLiteral("Bring the Whatly window to the front");
  Binding compose;
  compose.id = QStringLiteral("quick-compose");
  compose.trigger = QStringLiteral("CTRL+ALT+n");
  compose.description = QStringLiteral("Open the Whatly quick-compose box");
#if defined(Q_OS_LINUX)
  raise.keysym = XK_w;
  compose.keysym = XK_n;
#endif
  m_bindings << raise << compose;
}

GlobalShortcut::~GlobalShortcut() { ungrabX11(); }

bool GlobalShortcut::tryRegister() {
  if (tryPortal())
    return true;
  return tryX11();
}

// ── Portal backend ──────────────────────────────────────────────────────────
bool GlobalShortcut::tryPortal() {
#if defined(Q_OS_LINUX)
  QDBusConnection bus = QDBusConnection::sessionBus();
  QDBusInterface probe(kPortalService, kPortalPath, kPortalIface, bus);
  if (!probe.isValid())
    return false; // no portal (or no backend implementing GlobalShortcuts)

  qDBusRegisterMetaType<PortalShortcut>();
  qDBusRegisterMetaType<QList<PortalShortcut>>();

  // Fires whenever any bound shortcut in our session is pressed.
  bus.connect(kPortalService, kPortalPath, kPortalIface, "Activated", this,
              SLOT(onPortalActivated(QDBusObjectPath, QString, qulonglong,
                                     QVariantMap)));

  QVariantMap opts;
  opts["handle_token"] = QStringLiteral("whatly_gs_create");
  opts["session_handle_token"] = QStringLiteral("whatly_gs_session");
  QDBusReply<QDBusObjectPath> reply = probe.call("CreateSession", opts);
  if (!reply.isValid())
    return false;

  // The reply is a Request object; its Response signal carries session_handle.
  bus.connect(kPortalService, reply.value().path(),
              "org.freedesktop.portal.Request", "Response", this,
              SLOT(onCreateSessionResponse(uint, QVariantMap)));
  return true;
#else
  return false;
#endif
}

#if defined(Q_OS_LINUX)
void GlobalShortcut::onCreateSessionResponse(uint response,
                                             const QVariantMap &results) {
  if (response != 0) // 0 = success; 1 = cancelled; 2 = other
    return;
  m_sessionHandle = results.value("session_handle").toString();
  if (m_sessionHandle.isEmpty())
    return;

  QList<PortalShortcut> shortcuts;
  for (const Binding &b : m_bindings) {
    PortalShortcut sc;
    sc.id = b.id;
    sc.meta["description"] = b.description;
    sc.meta["preferred_trigger"] = b.trigger;
    shortcuts << sc;
  }

  QVariantMap bindOpts;
  bindOpts["handle_token"] = QStringLiteral("whatly_gs_bind");
  QDBusMessage msg = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kPortalIface, "BindShortcuts");
  msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
      << QVariant::fromValue(shortcuts)
      << QString() // parent_window
      << bindOpts;

  QDBusConnection bus = QDBusConnection::sessionBus();
  QDBusReply<QDBusObjectPath> reply = bus.call(msg);
  if (reply.isValid())
    bus.connect(kPortalService, reply.value().path(),
                "org.freedesktop.portal.Request", "Response", this,
                SLOT(onBindResponse(uint, QVariantMap)));
}

void GlobalShortcut::onBindResponse(uint response, const QVariantMap &) {
  if (response == 0)
    qInfo() << "Global shortcuts bound via the desktop portal.";
  else
    qInfo() << "The desktop portal declined the global shortcuts (response"
            << response << ").";
}

void GlobalShortcut::onPortalActivated(const QDBusObjectPath &,
                                       const QString &shortcutId, qulonglong,
                                       const QVariantMap &) {
  for (const Binding &b : m_bindings)
    if (shortcutId == b.id) {
      emit activated(b.id);
      return;
    }
}
#endif

// ── X11 fallback backend ────────────────────────────────────────────────────
bool GlobalShortcut::tryX11() {
#if defined(Q_OS_LINUX)
  if (QGuiApplication::platformName() != QLatin1String("xcb"))
    return false;
  Display *dpy = x11Display();
  if (!dpy)
    return false;
  m_modifiers = ControlMask | Mod1Mask;
  const Window root = DefaultRootWindow(dpy);
  XErrorHandler prev = XSetErrorHandler(ignoreXError);
  bool any = false;
  for (Binding &b : m_bindings) {
    b.keycode = XKeysymToKeycode(dpy, b.keysym);
    if (b.keycode == 0)
      continue;
    for (unsigned int lock : kLockMasks)
      XGrabKey(dpy, b.keycode, m_modifiers | lock, root, 1, GrabModeAsync,
               GrabModeAsync);
    any = true;
  }
  XSync(dpy, 0);
  XSetErrorHandler(prev);
  if (!any)
    return false;
  qApp->installNativeEventFilter(this);
  m_x11Registered = true;
  qInfo() << "Global shortcuts registered via X11 key grabs.";
  return true;
#else
  return false;
#endif
}

void GlobalShortcut::ungrabX11() {
#if defined(Q_OS_LINUX)
  if (!m_x11Registered)
    return;
  if (Display *dpy = x11Display()) {
    const Window root = DefaultRootWindow(dpy);
    for (const Binding &b : m_bindings)
      if (b.keycode)
        for (unsigned int lock : kLockMasks)
          XUngrabKey(dpy, b.keycode, m_modifiers | lock, root);
    XSync(dpy, 0);
  }
  qApp->removeNativeEventFilter(this);
  m_x11Registered = false;
#endif
}

bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType,
                                       void *message, qintptr *) {
#if defined(Q_OS_LINUX)
  if (!m_x11Registered ||
      eventType != QByteArrayLiteral("xcb_generic_event_t"))
    return false;
  auto *ev = static_cast<xcb_generic_event_t *>(message);
  if ((ev->response_type & ~0x80) == XCB_KEY_PRESS) {
    auto *ke = reinterpret_cast<xcb_key_press_event_t *>(ev);
    const unsigned int mask = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;
    if ((ke->state & mask) == m_modifiers)
      for (const Binding &b : m_bindings)
        if (b.keycode && ke->detail == b.keycode) {
          emit activated(b.id);
          break;
        }
  }
#else
  Q_UNUSED(eventType);
  Q_UNUSED(message);
#endif
  return false;
}
