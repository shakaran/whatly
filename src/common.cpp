#include "common.h"

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
