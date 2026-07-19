#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <QString>

class QSettings;
class QWebEngineProfile;

// Machine-wide performance / GPU / privacy tuning for the bundled Chromium.
//
// Whatly historically hard-coded --disable-gpu on Linux to dodge a class of
// blank-window/crash-on-start bugs on some GPU/driver/Wayland/Flatpak setups.
// That is still the default, but every knob is now exposed so a user whose GPU
// works can turn acceleration back on, and one whose setup is unusual can pick
// the workaround that fits — instead of a blanket switch.
//
// These are stored globally (not per-account) and read once, before QApplication
// exists (Chromium flags are start-up only), so changing them needs a restart.
namespace Performance {

// The shared, machine-wide settings object (the default profile's file).
QSettings &settings();

// GPU / process model (each maps to a well-known Chromium switch).
bool disableGpu();                 // --disable-gpu (default: true on Linux)
bool disableGpuCompositing();      // --disable-gpu-compositing (default: true)
bool disableGpuVsync();            // --disable-gpu-vsync
bool inProcessGpu();               // --in-process-gpu
bool ignoreGpuBlocklist();         // --ignore-gpu-blocklist
bool singleProcess();              // --single-process
bool processPerSite();             // --process-per-site

// Privacy: stop WebRTC from leaking the local IP over non-proxied UDP.
bool webrtcShield();               // --force-webrtc-ip-handling-policy=...

// V8 heap cap in MB (0 = Chromium default) — helps the "eats RAM" reports.
int jsMemoryLimitMb();

// HTTP cache: "disk" (default), "memory" or "none"; max size in MB (0 = auto).
QString cacheType();
int cacheMaxMb();

// Setters (used by the Settings UI).
void setDisableGpu(bool v);
void setDisableGpuCompositing(bool v);
void setDisableGpuVsync(bool v);
void setInProcessGpu(bool v);
void setIgnoreGpuBlocklist(bool v);
void setSingleProcess(bool v);
void setProcessPerSite(bool v);
void setWebrtcShield(bool v);
void setJsMemoryLimitMb(int mb);
void setCacheType(const QString &type);
void setCacheMaxMb(int mb);

// Build the extra QTWEBENGINE_CHROMIUM_FLAGS fragment from the settings above.
// Pure function of the stored values, so it is unit-tested directly.
QString chromiumFlagFragment();

// Apply the HTTP-cache choice to a profile (done when the profile is built).
void applyToProfile(QWebEngineProfile *profile);

} // namespace Performance

#endif // PERFORMANCE_H
