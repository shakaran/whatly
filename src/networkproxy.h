#ifndef NETWORKPROXY_H
#define NETWORKPROXY_H

#include <QString>

class QSettings;

// Machine-wide network-proxy configuration for the whole application (and thus
// every WhatsApp Web account, since Qt WebEngine honours the application proxy).
//
// Modes:
//   "system"  – use the OS proxy configuration (the default; unchanged behaviour)
//   "none"    – connect directly, ignoring any system proxy
//   "socks5"  – a SOCKS5 proxy at host:port (optionally authenticated)
//   "http"    – an HTTP proxy at host:port (optionally authenticated)
//
// applyToApplication() can be called at start-up and again whenever the settings
// change; switching to/from a manual proxy takes effect for new connections.
namespace NetworkProxy {

QSettings &settings();

QString mode();          // "system" (default) | "none" | "socks5" | "http"
QString host();
int port();
QString user();
QString password();

void setMode(const QString &mode);
void setHost(const QString &host);
void setPort(int port);
void setUser(const QString &user);
void setPassword(const QString &password);

// Install the configured proxy as the application-wide proxy.
void applyToApplication();

} // namespace NetworkProxy

#endif // NETWORKPROXY_H
