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

// Route WebRTC screen capture through PipeWire + the desktop portal, which is
// the only way screen sharing works on Wayland. Default on for Linux.
bool webrtcPipeWire();             // --enable-features=WebRTCPipeWireCapturer

// V8 heap cap in MB (0 = Chromium default) — helps the "eats RAM" reports.
int jsMemoryLimitMb();

// Ask V8 to trade a little speed for a smaller heap (--js-flags=--optimize-for-
// size). Sensible default for an always-on tray app whose renderer sits idle in
// the background most of the time; trims the baseline QtWebEngineProcess
// footprint (issue #15). Default: true.
bool optimizeForSize();

// HTTP cache: "disk" (default), "memory" or "none"; max size in MB (0 = auto).
QString cacheType();
int cacheMaxMb();

// Font hinting for the page: "" (default, follow the system) or one of
// none/slight/medium/full, mapped to --font-render-hinting (issue #37).
QString fontHinting();

// Suspend (freeze) background account pages after they have been idle, to cut
// memory with multiple accounts. Off by default; a suspended account does not
// receive messages until it is reopened.
bool suspendInactiveAccounts();
int suspendAfterMinutes();
// Pure decision used by the suspend timer and unit tested: freeze only a
// background (non-active, off-screen) account idle past the threshold.
bool shouldSuspendAccount(bool enabled, bool isActive, bool isVisible,
                          int idleSecs, int thresholdSecs);
// Extend that unloading to whole windows that are off screen: the account a
// minimised window — or one put away to the tray — was showing is unloaded as
// well, and built again when the window comes back. Off by default, and inert
// unless the option above is on (issue #25).
bool unloadOffscreenWindows();
// Pure decision, unit tested. Same delay as above, counted from when the window
// went away rather than from when the account was last read: the account on
// screen was being read until the moment the window went, so its own idle clock
// would say "just now" for a window that has been gone all afternoon.
bool shouldUnloadWithWindow(bool enabled, bool alsoOffscreen,
                            bool windowOffscreen, int awaySecs,
                            int thresholdSecs);

// Interface/content scale factor (feeds QT_SCALE_FACTOR + --force-device-scale-
// factor, matching #203). 0 = automatic (let the environment/desktop decide).
double interfaceScaleFactor();

// Setters (used by the Settings UI).
void setDisableGpu(bool v);
void setDisableGpuCompositing(bool v);
void setDisableGpuVsync(bool v);
void setInProcessGpu(bool v);
void setIgnoreGpuBlocklist(bool v);
void setSingleProcess(bool v);
void setProcessPerSite(bool v);
void setWebrtcShield(bool v);
void setWebrtcPipeWire(bool v);
void setJsMemoryLimitMb(int mb);
void setOptimizeForSize(bool v);
void setCacheType(const QString &type);
void setFontHinting(const QString &level);
void setSuspendInactiveAccounts(bool v);
void setSuspendAfterMinutes(int m);
void setUnloadOffscreenWindows(bool v);
void setCacheMaxMb(int mb);
void setInterfaceScaleFactor(double factor);

// ── Start-up crash recovery ("safe rendering") ─────────────────────────────
// A relocated/bundled Qt WebEngine can hard-abort (SIGTRAP) while initialising
// GPU/GL against an incompatible system driver, before WhatsApp Web ever loads
// (issue #3: RPM on Fedora 44). These track that across launches and escalate
// to progressively safer rendering flags so the app heals itself instead of
// crashing on every start.

// Read once at start-up, before the flags are built. If the previous launch
// armed the watch but never reported success, that is treated as a crash: the
// recovery level is bumped and persisted, and the pending flag is consumed so a
// single crash is counted once. Safe to call from any invocation.
void evaluateStartup();

// Current recovery level: 0 = normal, higher = safer (and slower) rendering.
int recoveryLevel();

// Mark that a GUI launch is about to load the page. Call right before the
// window is shown, on the primary GUI path only.
void armStartupWatch();

// WhatsApp Web loaded successfully: clear the pending flag and reset recovery.
void markStartupSucceeded();

// Build the extra QTWEBENGINE_CHROMIUM_FLAGS fragment from the settings above,
// plus any start-up recovery flags. Pure function of the stored values, so it
// is unit-tested directly.
QString chromiumFlagFragment();

// Apply the HTTP-cache choice to a profile (done when the profile is built).
void applyToProfile(QWebEngineProfile *profile);

} // namespace Performance

#endif // PERFORMANCE_H
