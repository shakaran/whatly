#include "networkproxy.h"

#include <QNetworkProxy>
#include <QSettings>

namespace NetworkProxy {

QSettings &settings() {
  // Same machine-wide store as the rest of the global settings.
  static QSettings s(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("shakaran"), QStringLiteral("whatly"));
  return s;
}

QString mode() {
  return settings().value(QStringLiteral("proxy/mode"),
                          QStringLiteral("system")).toString();
}
QString host() {
  return settings().value(QStringLiteral("proxy/host")).toString();
}
int port() { return settings().value(QStringLiteral("proxy/port"), 0).toInt(); }
QString user() {
  return settings().value(QStringLiteral("proxy/user")).toString();
}
QString password() {
  return settings().value(QStringLiteral("proxy/password")).toString();
}

void setMode(const QString &m) { settings().setValue(QStringLiteral("proxy/mode"), m); }
void setHost(const QString &h) { settings().setValue(QStringLiteral("proxy/host"), h); }
void setPort(int p) {
  settings().setValue(QStringLiteral("proxy/port"), qBound(0, p, 65535));
}
void setUser(const QString &u) { settings().setValue(QStringLiteral("proxy/user"), u); }
void setPassword(const QString &p) {
  settings().setValue(QStringLiteral("proxy/password"), p);
}

void applyToApplication() {
  const QString m = mode();
  if (m == QLatin1String("none")) {
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    return;
  }
  if (m == QLatin1String("socks5") || m == QLatin1String("http")) {
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy proxy(m == QLatin1String("socks5")
                            ? QNetworkProxy::Socks5Proxy
                            : QNetworkProxy::HttpProxy,
                        host(), static_cast<quint16>(port()));
    if (!user().isEmpty())
      proxy.setUser(user());
    if (!password().isEmpty())
      proxy.setPassword(password());
    QNetworkProxy::setApplicationProxy(proxy);
    return;
  }
  // "system" (or anything unrecognised): defer to the OS configuration.
  QNetworkProxyFactory::setUseSystemConfiguration(true);
}

} // namespace NetworkProxy
