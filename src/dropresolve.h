#ifndef DROPRESOLVE_H
#define DROPRESOLVE_H

#include <QStringList>

class QMimeData;

// Turning a drag-and-drop into readable file paths, kept free of any WebEngine
// dependency so it can be unit tested. See issues #32 (Flatpak) and #34.
namespace DropResolve {

// Readable local file paths for a drop. A normal (non-sandboxed) install gets
// the real file:// paths and reads them directly, so no portal is touched --
// which also means nothing to fail on desktops whose portal lacks the
// FileTransfer interface (#34). Only when none of the dropped URLs are readable
// here (the Flatpak sandbox sees host paths it cannot open) does it resolve them
// through the XDG FileTransfer portal, which hands back readable copies under
// /run/user/<uid>/doc/ (issue #32).
QStringList droppedFilePaths(const QMimeData *mime);

} // namespace DropResolve

#endif // DROPRESOLVE_H
