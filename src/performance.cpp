#include "performance.h"

#include <QSettings>
#include <QStringList>
#include <QWebEngineProfile>

namespace {
// Defaults preserve Whatly's historical behaviour: on Linux the GPU and GPU
// compositing are off (the long-standing blank-window workaround); on Windows
// the GPU stays on but compositing is off.
#ifdef Q_OS_WIN
constexpr bool kDefaultDisableGpu = false;
#else
constexpr bool kDefaultDisableGpu = true;
#endif
constexpr bool kDefaultDisableGpuCompositing = true;

bool b(const QString &key, bool def) {
  return Performance::settings().value(key, def).toBool();
}
void setB(const QString &key, bool v) {
  Performance::settings().setValue(key, v);
}
} // namespace

namespace Performance {

QSettings &settings() {
  // Machine-wide: always the default profile's file, whatever account is active.
  static QSettings s(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("shakaran"), QStringLiteral("whatly"));
  return s;
}

bool disableGpu() { return b(QStringLiteral("perf/disableGpu"), kDefaultDisableGpu); }
bool disableGpuCompositing() {
  return b(QStringLiteral("perf/disableGpuCompositing"), kDefaultDisableGpuCompositing);
}
bool disableGpuVsync() { return b(QStringLiteral("perf/disableGpuVsync"), false); }
bool inProcessGpu() { return b(QStringLiteral("perf/inProcessGpu"), false); }
bool ignoreGpuBlocklist() { return b(QStringLiteral("perf/ignoreGpuBlocklist"), false); }
bool singleProcess() { return b(QStringLiteral("perf/singleProcess"), false); }
bool processPerSite() { return b(QStringLiteral("perf/processPerSite"), false); }
bool webrtcShield() { return b(QStringLiteral("perf/webrtcShield"), false); }

int jsMemoryLimitMb() {
  return settings().value(QStringLiteral("perf/jsMemoryLimitMb"), 0).toInt();
}
QString cacheType() {
  return settings().value(QStringLiteral("perf/cacheType"), QStringLiteral("disk")).toString();
}
int cacheMaxMb() { return settings().value(QStringLiteral("perf/cacheMaxMb"), 0).toInt(); }
double interfaceScaleFactor() {
  return settings().value(QStringLiteral("perf/interfaceScaleFactor"), 0.0).toDouble();
}

void setDisableGpu(bool v) { setB(QStringLiteral("perf/disableGpu"), v); }
void setDisableGpuCompositing(bool v) { setB(QStringLiteral("perf/disableGpuCompositing"), v); }
void setDisableGpuVsync(bool v) { setB(QStringLiteral("perf/disableGpuVsync"), v); }
void setInProcessGpu(bool v) { setB(QStringLiteral("perf/inProcessGpu"), v); }
void setIgnoreGpuBlocklist(bool v) { setB(QStringLiteral("perf/ignoreGpuBlocklist"), v); }
void setSingleProcess(bool v) { setB(QStringLiteral("perf/singleProcess"), v); }
void setProcessPerSite(bool v) { setB(QStringLiteral("perf/processPerSite"), v); }
void setWebrtcShield(bool v) { setB(QStringLiteral("perf/webrtcShield"), v); }
void setJsMemoryLimitMb(int mb) {
  settings().setValue(QStringLiteral("perf/jsMemoryLimitMb"), qMax(0, mb));
}
void setCacheType(const QString &type) {
  settings().setValue(QStringLiteral("perf/cacheType"), type);
}
void setCacheMaxMb(int mb) {
  settings().setValue(QStringLiteral("perf/cacheMaxMb"), qMax(0, mb));
}
void setInterfaceScaleFactor(double factor) {
  settings().setValue(QStringLiteral("perf/interfaceScaleFactor"),
                      factor < 0.0 ? 0.0 : factor);
}

QString chromiumFlagFragment() {
  QStringList f;
  if (disableGpu())
    f << QStringLiteral("--disable-gpu");
  if (disableGpuCompositing())
    f << QStringLiteral("--disable-gpu-compositing");
  if (disableGpuVsync())
    f << QStringLiteral("--disable-gpu-vsync");
  if (inProcessGpu())
    f << QStringLiteral("--in-process-gpu");
  if (ignoreGpuBlocklist())
    f << QStringLiteral("--ignore-gpu-blocklist");
  if (singleProcess())
    f << QStringLiteral("--single-process");
  if (processPerSite())
    f << QStringLiteral("--process-per-site");
  if (webrtcShield())
    f << QStringLiteral(
        "--force-webrtc-ip-handling-policy=disable_non_proxied_udp");
  if (const int mb = jsMemoryLimitMb(); mb > 0)
    f << QStringLiteral("--js-flags=--max-old-space-size=%1").arg(mb);
  return f.join(QLatin1Char(' '));
}

void applyToProfile(QWebEngineProfile *profile) {
  if (!profile)
    return;
  const QString type = cacheType();
  if (type == QLatin1String("memory"))
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
  else if (type == QLatin1String("none"))
    profile->setHttpCacheType(QWebEngineProfile::NoCache);
  else
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);

  if (const int mb = cacheMaxMb(); mb > 0)
    profile->setHttpCacheMaximumSize(static_cast<int>(mb) * 1024 * 1024);
}

} // namespace Performance
