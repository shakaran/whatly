#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <QString>

class QWebEngineProfile;

// Opt-in, off-by-default instrumentation a user can switch on to gather figures
// for a bug report. The scroll-smoothness probe (from debug/, embedded so it is
// the same file its harnesses test) is injected into the page while the setting
// is on, records to the page's Local Storage, and is read back out on demand so
// it can be pasted into an issue. Nothing runs, and nothing is injected, while
// the setting is off.
namespace Diagnostics {

bool scrollProbeEnabled();
void setScrollProbeEnabled(bool on);

// The bundled scroll-smoothness probe, read from the embedded resource.
QString scrollProbeScript();

// A small expression that returns the probe's stored result (the JSON it keeps
// in Local Storage) as a string, or an empty string if there is none yet.
QString readbackScript();

// Add the probe to the profile's script collection, or remove it, to match the
// setting — the same mechanism CustomJs::install() uses, so it applies to every
// account on that profile and survives reloads. Call at start-up and whenever
// the setting changes.
void install(QWebEngineProfile *profile);

} // namespace Diagnostics

#endif // DIAGNOSTICS_H
