#ifndef COMMON_H
#define COMMON_H
#include <QString>
#include <QIcon>

// userAgent
extern QString defaultUserAgentStr;

// The origin every page permission belongs to.
extern const QString whatsAppOrigin;

// appAutoLock
extern int defaultAppAutoLockDuration;
extern bool defaultAppAutoLock;
extern double defaultZoomFactorMaximized;

QIcon themeIcon(const QString& name, const QString& fallback);

// Page-zoom bounds for Ctrl +/-/0 and the injected zoom buttons; the zoom is
// clamped so it can never become unusably tiny or huge. Pure, so it is unit
// tested directly.
constexpr double kMinZoomFactor = 0.3;
constexpr double kMaxZoomFactor = 3.0;
inline double clampZoom(double factor) {
  return factor < kMinZoomFactor   ? kMinZoomFactor
         : factor > kMaxZoomFactor ? kMaxZoomFactor
                                   : factor;
}


#endif // COMMON_H

