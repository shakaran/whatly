#ifndef AUTOSTART_H
#define AUTOSTART_H

// "Start Whatly automatically when you log in."
//
// On Linux this writes/removes an XDG autostart entry at
// ~/.config/autostart/net.shakaran.whatly.desktop. On Windows it toggles a
// value under HKCU\...\Run. Elsewhere the calls are safe no-ops.
namespace Autostart {

// Whether an autostart entry currently exists for this app.
bool isEnabled();

// Create (enable) or remove (disable) the autostart entry. Returns true on
// success. When enabled, the entry launches the app with --hidden so it starts
// minimised to the tray.
bool setEnabled(bool enabled);

// True on platforms where autostart is implemented (Linux, Windows).
bool isSupported();

} // namespace Autostart

#endif // AUTOSTART_H
