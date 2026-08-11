#ifndef SESSIONBACKUP_H
#define SESSIONBACKUP_H

#include <QString>
#include <QStringList>

// Keeps a linked WhatsApp Web session alive across a corrupt profile.
//
// When QtWebEngine's IndexedDB LevelDB goes corrupt ("Corruption: checksum
// mismatch"), Chromium recovers by *deleting* the database — which throws away
// the multi-device session keys, so the next launch shows a QR code and the
// user has to re-link the phone. This module keeps a small, recent copy of the
// session-bearing storage (IndexedDB + Local Storage, not the caches) and, when
// it sees a wiped session with a good snapshot behind it, restores it.
//
// Everything runs at startup, BEFORE any QWebEngineProfile is created, so no
// database is open while it copies — the same open-file hazard that corrupts a
// live LevelDB would otherwise make the snapshot itself inconsistent. See #43.
namespace SessionBackup {

// The storage subfolders that carry a session. Deliberately small: caches and
// the service worker are re-fetched and would only bloat the snapshot. Pure.
QStringList sessionSubdirs();

// The engine storage subpath for an account relative to the app-data root:
// "/QtWebEngine" for the default account, plus the process --profile suffix and
// the account id where present. Mirrors WebEngineProfileManager. Pure.
QString engineSubdir(const QString &profileSuffix, const QString &accountId);

// A filesystem-safe key for an account's snapshot folder ("default" for the
// unnamed account, else the account id). Pure.
QString snapshotKey(const QString &accountId);

// Below this many bytes an IndexedDB store holds no usable session (a fresh or
// Chromium-wiped profile); at or above it a session is present. Pure.
bool sessionLooksPresent(qint64 indexedDbBytes);

// Whether there is enough free disk to snapshot/restore safely. On a nearly-full
// volume a copy truncates the LevelDB it duplicates ("partial record"
// corruption), so backup backs off below the threshold. Pure. See #43.
bool hasEnoughFreeSpace(qint64 freeBytes);

// Copy an account's session subdirs into its snapshot, keeping the previous
// generation as a fallback. No-op (returns false) when the live session is
// absent, so a wiped profile never overwrites a good snapshot. Skips the copy
// when nothing changed since the last snapshot.
bool snapshot(const QString &accountId);

// Restore an account's newest healthy snapshot over its live profile. Returns
// false when there is no healthy snapshot to restore from.
bool restore(const QString &accountId);

// Run once at startup, before any profile is created: for each account,
// auto-restore a wiped session from its snapshot or refresh the snapshot of a
// healthy one. Honours the "sessionBackup/enabled" setting (default on).
void runStartupRecovery();

// Test seam: point the effectful helpers at a temp app-data root and profile
// suffix instead of the real QStandardPaths locations. Empty root restores the
// default (real) locations.
void setPathsForTesting(const QString &dataRoot, const QString &profileSuffix);

} // namespace SessionBackup

#endif // SESSIONBACKUP_H
