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

// Cap on how far start-up crash recovery escalates the rendering flags.
constexpr int kMaxRecoveryLevel = 2;

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
bool webrtcPipeWire() {
#ifdef Q_OS_LINUX
  return b(QStringLiteral("perf/webrtcPipeWire"), true);
#else
  return b(QStringLiteral("perf/webrtcPipeWire"), false);
#endif
}

int jsMemoryLimitMb() {
  return settings().value(QStringLiteral("perf/jsMemoryLimitMb"), 0).toInt();
}
bool optimizeForSize() {
  return b(QStringLiteral("perf/optimizeForSize"), true);
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
void setWebrtcPipeWire(bool v) { setB(QStringLiteral("perf/webrtcPipeWire"), v); }
void setJsMemoryLimitMb(int mb) {
  settings().setValue(QStringLiteral("perf/jsMemoryLimitMb"), qMax(0, mb));
}
void setOptimizeForSize(bool v) {
  setB(QStringLiteral("perf/optimizeForSize"), v);
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

void evaluateStartup() {
  QSettings &s = settings();
  if (!s.value(QStringLiteral("perf/startupPending"), false).toBool())
    return;
  // The previous GUI launch armed the watch but never cleared it → it crashed
  // before the page loaded. Bump the recovery level (capped) and consume the
  // pending flag so this crash is counted exactly once.
  const int level = qMin(
      kMaxRecoveryLevel,
      s.value(QStringLiteral("perf/recoveryLevel"), 0).toInt() + 1);
  s.setValue(QStringLiteral("perf/recoveryLevel"), level);
  s.setValue(QStringLiteral("perf/startupPending"), false);
  s.sync();
}

int recoveryLevel() {
  return qBound(
      0, settings().value(QStringLiteral("perf/recoveryLevel"), 0).toInt(),
      kMaxRecoveryLevel);
}

void armStartupWatch() {
  settings().setValue(QStringLiteral("perf/startupPending"), true);
  settings().sync();
}

void markStartupSucceeded() {
  QSettings &s = settings();
  s.setValue(QStringLiteral("perf/startupPending"), false);
  s.setValue(QStringLiteral("perf/recoveryLevel"), 0);
  s.sync();
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
  if (webrtcPipeWire())
    f << QStringLiteral("--enable-features=WebRTCPipeWireCapturer");
  // V8 heap tuning, packed into a single --js-flags token. Qt tokenises
  // QTWEBENGINE_CHROMIUM_FLAGS on whitespace, so the value passed to Chromium
  // must not itself contain a space — only one sub-flag can travel here. An
  // explicit heap cap is the stronger, user-chosen bound, so it wins the slot
  // when set; otherwise optimize-for-size trims the baseline heap by default.
  if (const int mb = jsMemoryLimitMb(); mb > 0)
    f << QStringLiteral("--js-flags=--max-old-space-size=%1").arg(mb);
  else if (optimizeForSize())
    f << QStringLiteral("--js-flags=--optimize-for-size");

  // Start-up crash recovery (issue #3): after a crash before the page loaded,
  // force progressively safer rendering. Level 1 drops all GPU/GL use for the
  // pure-software compositor (which renders WhatsApp Web's 2D content fine);
  // level 2 also folds the GPU work into the browser process. Deduplicated
  // against whatever the user's own GPU settings already added.
  if (const int level = recoveryLevel(); level > 0) {
    QStringList safe{QStringLiteral("--disable-gpu"),
                     QStringLiteral("--disable-gpu-compositing"),
                     QStringLiteral("--disable-software-rasterizer")};
    if (level >= 2)
      safe << QStringLiteral("--in-process-gpu")
           << QStringLiteral("--disable-gpu-sandbox");
    for (const QString &flag : std::as_const(safe))
      if (!f.contains(flag))
        f << flag;
  }
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
