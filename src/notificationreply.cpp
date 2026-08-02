#include "notificationreply.h"

#include <QImage>

namespace NotificationReplyUtil {
bool hasInlineReply(const QStringList &capabilities) {
  return capabilities.contains(QStringLiteral("inline-reply"));
}
} // namespace NotificationReplyUtil

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QVariantMap>

namespace {
const char kService[] = "org.freedesktop.Notifications";
const char kPath[] = "/org/freedesktop/Notifications";
const char kIface[] = "org.freedesktop.Notifications";

// The "image-data" hint value: the (iiibiiay) structure from the FDO spec.
struct ImageData {
  int width = 0, height = 0, rowstride = 0;
  bool hasAlpha = false;
  int bitsPerSample = 8, channels = 4;
  QByteArray data;
};

QDBusArgument &operator<<(QDBusArgument &a, const ImageData &i) {
  a.beginStructure();
  a << i.width << i.height << i.rowstride << i.hasAlpha << i.bitsPerSample
    << i.channels << i.data;
  a.endStructure();
  return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, ImageData &i) {
  a.beginStructure();
  a >> i.width >> i.height >> i.rowstride >> i.hasAlpha >> i.bitsPerSample >>
      i.channels >> i.data;
  a.endStructure();
  return a;
}

ImageData toImageData(const QImage &src) {
  ImageData d;
  if (src.isNull())
    return d;
  const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
  d.width = img.width();
  d.height = img.height();
  d.rowstride = img.bytesPerLine();
  d.hasAlpha = true;
  d.bitsPerSample = 8;
  d.channels = 4;
  d.data = QByteArray(reinterpret_cast<const char *>(img.constBits()),
                      static_cast<int>(img.sizeInBytes()));
  return d;
}
} // namespace

Q_DECLARE_METATYPE(ImageData)
#endif // Q_OS_LINUX

NotificationReply::NotificationReply(QObject *parent) : QObject(parent) {
#ifdef Q_OS_LINUX
  qDBusRegisterMetaType<ImageData>();
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected())
    return;

  QDBusInterface iface(QLatin1String(kService), QLatin1String(kPath),
                       QLatin1String(kIface), bus);
  if (!iface.isValid())
    return;
  const QDBusReply<QStringList> caps = iface.call(QStringLiteral("GetCapabilities"));
  if (!caps.isValid())
    return;
  m_available = NotificationReplyUtil::hasInlineReply(caps.value());
  if (!m_available)
    return;

  // Route the three signals we care about back out as Qt signals.
  bus.connect(QLatin1String(kService), QLatin1String(kPath),
              QLatin1String(kIface), QStringLiteral("NotificationReplied"), this,
              SIGNAL(replied(uint, QString)));
  bus.connect(QLatin1String(kService), QLatin1String(kPath),
              QLatin1String(kIface), QStringLiteral("ActionInvoked"), this,
              SIGNAL(actionInvoked(uint, QString)));
  bus.connect(QLatin1String(kService), QLatin1String(kPath),
              QLatin1String(kIface), QStringLiteral("NotificationClosed"), this,
              SIGNAL(closed(uint, uint)));
#endif
}

quint32 NotificationReply::notify(const QString &appName, const QString &title,
                                  const QString &body, const QImage &icon,
                                  int timeoutMs, const QString &replyLabel,
                                  const QString &replyPlaceholder) {
#ifdef Q_OS_LINUX
  if (!m_available)
    return 0;
  QDBusInterface iface(QLatin1String(kService), QLatin1String(kPath),
                       QLatin1String(kIface), QDBusConnection::sessionBus());
  if (!iface.isValid())
    return 0;

  // "default" fires on a plain click; "inline-reply" is the reply field.
  QStringList actions;
  actions << QStringLiteral("inline-reply") << replyLabel
          << QStringLiteral("default") << QStringLiteral("Open");

  QVariantMap hints;
  hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("whatly"));
  if (!replyPlaceholder.isEmpty())
    hints.insert(QStringLiteral("x-kde-reply-placeholder-text"),
                 replyPlaceholder);
  if (!icon.isNull())
    hints.insert(QStringLiteral("image-data"),
                 QVariant::fromValue(toImageData(icon)));

  const QDBusReply<uint> reply = iface.call(
      QStringLiteral("Notify"), appName, uint(0), QStringLiteral("whatly"),
      title, body, actions, hints, timeoutMs);
  return reply.isValid() ? reply.value() : 0;
#else
  Q_UNUSED(appName) Q_UNUSED(title) Q_UNUSED(body) Q_UNUSED(icon)
  Q_UNUSED(timeoutMs) Q_UNUSED(replyLabel) Q_UNUSED(replyPlaceholder)
  return 0;
#endif
}

void NotificationReply::close(quint32 id) {
#ifdef Q_OS_LINUX
  if (!id)
    return;
  QDBusInterface iface(QLatin1String(kService), QLatin1String(kPath),
                       QLatin1String(kIface), QDBusConnection::sessionBus());
  if (iface.isValid())
    iface.call(QStringLiteral("CloseNotification"), id);
#else
  Q_UNUSED(id)
#endif
}
