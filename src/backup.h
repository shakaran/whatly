#ifndef BACKUP_H
#define BACKUP_H

#include <QString>

// Export/import a whole account: its Qt WebEngine storage (the logged-in
// session), its settings file, and its custom CSS / JS addons — packed as a
// single .tar.gz. Import restores them (the app must then be restarted).
//
// The archive contains the live WhatsApp session, so it is as sensitive as the
// account itself; the UI warns before writing one.
//
// The archive/extract and recursive-copy primitives are pure filesystem
// operations and are unit-tested with temporary directories.
namespace Backup {

// tar -czf `archive` -C `sourceDir` .  (returns false + *error on failure)
bool makeArchive(const QString &sourceDir, const QString &archive,
                 QString *error);
// tar -xzf `archive` -C `destDir`
bool extractArchive(const QString &archive, const QString &destDir,
                    QString *error);
// Recursively copy the contents of `src` into `dst` (created if missing).
bool copyDirRecursive(const QString &src, const QString &dst, QString *error);

// Export the current account to `archive` (a .tar.gz path).
bool exportProfile(const QString &archive, QString *error);
// Restore an account from `archive`. Existing data is overwritten.
bool importProfile(const QString &archive, QString *error);

} // namespace Backup

#endif // BACKUP_H
