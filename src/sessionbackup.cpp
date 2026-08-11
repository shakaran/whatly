#include "sessionbackup.h"

#include "appprofile.h"
#include "backup.h"
#include "settingsmanager.h"
#include "storageinfo.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
// A linked session's IndexedDB is comfortably over a megabyte; a fresh or
// Chromium-wiped one is a handful of metadata files. 256 KB sits well clear of
// both, so neither a real session reads as wiped nor an empty one as present.
constexpr qint64 kSessionMinBytes = 256 * 1024;

// Overridable roots for testing. Empty means "use the real QStandardPaths".
QString g_dataRootOverride;
QString g_suffixOverride;
bool g_suffixOverridden = false;

QString dataRoot() {
  if (!g_dataRootOverride.isEmpty())
    return g_dataRootOverride;
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString profileSuffix() {
  return g_suffixOverridden ? g_suffixOverride : AppProfile::suffix();
}

QString profileDir(const QString &accountId) {
  return dataRoot() + SessionBackup::engineSubdir(profileSuffix(), accountId);
}

QString snapshotAccountDir(const QString &accountId) {
  return dataRoot() + QStringLiteral("/session-snapshots/") +
         SessionBackup::snapshotKey(accountId);
}

qint64 indexedDbBytes(const QString &root) {
  return StorageInfo::directorySize(root + QStringLiteral("/IndexedDB"));
}

// The newest generation whose session looks intact, or empty if neither does.
QString newestHealthyGeneration(const QString &accDir) {
  const QString current = accDir + QStringLiteral("/current");
  if (SessionBackup::sessionLooksPresent(indexedDbBytes(current)))
    return current;
  const QString previous = accDir + QStringLiteral("/previous");
  if (SessionBackup::sessionLooksPresent(indexedDbBytes(previous)))
    return previous;
  return QString();
}
} // namespace

namespace SessionBackup {

QStringList sessionSubdirs() {
  return {QStringLiteral("IndexedDB"), QStringLiteral("Local Storage")};
}

QString engineSubdir(const QString &profileSuffix, const QString &accountId) {
  QString sub = QStringLiteral("/QtWebEngine") + profileSuffix;
  if (!accountId.isEmpty())
    sub += QLatin1Char('-') + accountId;
  return sub;
}

QString snapshotKey(const QString &accountId) {
  return accountId.isEmpty() ? QStringLiteral("default") : accountId;
}

bool sessionLooksPresent(qint64 indexedDbBytes) {
  return indexedDbBytes >= kSessionMinBytes;
}

bool snapshot(const QString &accountId) {
  const QString live = profileDir(accountId);
  const qint64 liveBytes = ::indexedDbBytes(live);
  // Never let a wiped or missing session overwrite a good snapshot.
  if (!sessionLooksPresent(liveBytes))
    return false;

  const QString accDir = snapshotAccountDir(accountId);
  const QString current = accDir + QStringLiteral("/current");
  // Nothing changed since the last snapshot: skip the copy.
  if (::indexedDbBytes(current) == liveBytes &&
      StorageInfo::directorySize(current) > 0)
    return true;

  const QString incoming = accDir + QStringLiteral("/.incoming");
  QDir(incoming).removeRecursively();
  if (!QDir().mkpath(incoming))
    return false;

  for (const QString &sub : sessionSubdirs()) {
    const QString from = live + QLatin1Char('/') + sub;
    if (!QFileInfo::exists(from))
      continue;
    if (!Backup::copyDirRecursive(from, incoming + QLatin1Char('/') + sub,
                                  nullptr)) {
      QDir(incoming).removeRecursively();
      return false;
    }
  }

  // A copy that lost the session mid-flight is worse than none.
  if (!sessionLooksPresent(::indexedDbBytes(incoming))) {
    QDir(incoming).removeRecursively();
    return false;
  }

  // Rotate: current -> previous, incoming -> current. Keeps one fallback in
  // case the newest snapshot ever turns out to be the corrupt one.
  QDir(accDir + QStringLiteral("/previous")).removeRecursively();
  if (QFileInfo::exists(current))
    QDir().rename(current, accDir + QStringLiteral("/previous"));
  return QDir().rename(incoming, current);
}

bool restore(const QString &accountId) {
  const QString gen = newestHealthyGeneration(snapshotAccountDir(accountId));
  if (gen.isEmpty())
    return false;

  const QString live = profileDir(accountId);
  if (!QDir().mkpath(live))
    return false;

  bool restoredAny = false;
  for (const QString &sub : sessionSubdirs()) {
    const QString from = gen + QLatin1Char('/') + sub;
    if (!QFileInfo::exists(from))
      continue;
    const QString to = live + QLatin1Char('/') + sub;
    QDir(to).removeRecursively();
    if (Backup::copyDirRecursive(from, to, nullptr))
      restoredAny = true;
  }
  return restoredAny;
}

void runStartupRecovery() {
  QSettings &s = SettingsManager::instance().settings();
  if (!s.value(QStringLiteral("sessionBackup/enabled"), true).toBool())
    return;

  QStringList accounts;
  accounts << QString(); // the default account
  accounts << s.value(QStringLiteral("accounts/ids")).toStringList();

  int recovered = 0;
  for (const QString &accountId : accounts) {
    const bool liveHasSession =
        sessionLooksPresent(::indexedDbBytes(profileDir(accountId)));
    const bool haveSnapshot =
        !newestHealthyGeneration(snapshotAccountDir(accountId)).isEmpty();

    if (!liveHasSession && haveSnapshot) {
      if (restore(accountId)) {
        ++recovered;
        qWarning().noquote()
            << "session-backup: restored the wiped session for account"
            << snapshotKey(accountId)
            << "from its last snapshot (no re-link needed).";
      }
    } else if (liveHasSession) {
      snapshot(accountId);
    }
  }
  if (recovered > 0)
    qInfo() << "session-backup: recovered" << recovered << "account session(s).";
}

void setPathsForTesting(const QString &dataRoot, const QString &profileSuffix) {
  g_dataRootOverride = dataRoot;
  g_suffixOverride = profileSuffix;
  g_suffixOverridden = !dataRoot.isEmpty();
}

} // namespace SessionBackup
