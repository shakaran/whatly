#include "dropresolve.h"

#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QUrl>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>
#endif

namespace DropResolve {

QStringList droppedFilePaths(const QMimeData *mime) {
  if (!mime)
    return {};

  const QList<QUrl> urls = mime->urls();

  QStringList paths;
  for (const QUrl &url : urls)
    if (url.isLocalFile()) {
      const QString p = url.toLocalFile();
      if (QFileInfo(p).isReadable())
        paths << p;
    }
  if (!paths.isEmpty())
    return paths; // real, readable files: the common case, no portal needed

#ifdef Q_OS_LINUX
  const QString kPortalMime =
      QStringLiteral("application/vnd.portal.filetransfer");
  if (mime->hasFormat(kPortalMime)) {
    QString key = QString::fromUtf8(mime->data(kPortalMime));
    key.remove(QChar(u'\0'));
    key = key.trimmed();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileTransfer"),
                          QDBusConnection::sessionBus());
    if (portal.isValid() && !key.isEmpty()) {
      const QDBusReply<QStringList> reply =
          portal.call(QStringLiteral("RetrieveFiles"), key, QVariantMap());
      if (reply.isValid())
        paths = reply.value();
      else
        qWarning() << "whatly: FileTransfer portal RetrieveFiles failed:"
                   << reply.error().message();
    }
  }
#endif

  // Last resort: hand back any local-file URLs even if the readability probe
  // above was inconclusive, so behaviour never regresses below the old path.
  if (paths.isEmpty())
    for (const QUrl &url : urls)
      if (url.isLocalFile())
        paths << url.toLocalFile();
  return paths;
}

} // namespace DropResolve
