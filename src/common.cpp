#include "common.h"

#include <QObject>
#include <QStringList>
#include <QUrl>

// Define global variables here to avoid multiple definition errors.
// NOTE: this is only a fallback. At startup WebEngineProfileManager overrides
// defaultUserAgentStr with the engine's own UA (stripped of the QtWebEngine
// token) so the reported Chrome version always matches the installed Chromium
// and auto-updates on Qt WebEngine upgrades.
QString defaultUserAgentStr = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";

const QString whatsAppOrigin = QStringLiteral("https://web.whatsapp.com");

int defaultAppAutoLockDuration = 30;
bool defaultAppAutoLock = false;
double defaultZoomFactorMaximized = 1.00;

QIcon themeIcon(const QString& name, const QString& fallback) {
  return QIcon::fromTheme(name, QIcon(fallback));
}

bool isInAppPopupUrl(const QUrl &url) {
  return url.isValid() && url.host() == QLatin1String("web.whatsapp.com");
}

QString accountTabTooltipText(const QString &version, const QString &token) {
  QStringList lines;
  if (!version.isEmpty())
    lines << QObject::tr("WhatsApp Web %1").arg(version);
  if (!token.isEmpty())
    lines << QObject::tr("Build token: %1").arg(token);
  return lines.join(QLatin1Char('\n'));
}
