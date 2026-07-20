#include "backup.h"
#include "settingsmanager.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
bool runTar(const QStringList &args, QString *error) {
  QProcess p;
  p.start(QStringLiteral("tar"), args);
  if (!p.waitForStarted(5000)) {
    if (error)
      *error = QObject::tr("Could not run 'tar'");
    return false;
  }
  p.waitForFinished(120000);
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
    if (error)
      *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return false;
  }
  return true;
}
} // namespace

namespace Backup {

bool makeArchive(const QString &sourceDir, const QString &archive,
                 QString *error) {
  if (!QFileInfo::exists(sourceDir)) {
    if (error)
      *error = QObject::tr("Nothing to back up");
    return false;
  }
  return runTar({QStringLiteral("-czf"), archive, QStringLiteral("-C"),
                 sourceDir, QStringLiteral(".")},
                error);
}

bool extractArchive(const QString &archive, const QString &destDir,
                    QString *error) {
  if (!QDir().mkpath(destDir)) {
    if (error)
      *error = QObject::tr("Cannot create %1").arg(destDir);
    return false;
  }
  return runTar({QStringLiteral("-xzf"), archive, QStringLiteral("-C"), destDir},
                error);
}

bool copyDirRecursive(const QString &src, const QString &dst, QString *error) {
  QDir().mkpath(dst);
  QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QString rel = QDir(src).relativeFilePath(it.filePath());
    const QString target = dst + QLatin1Char('/') + rel;
    if (it.fileInfo().isDir()) {
      if (!QDir().mkpath(target)) {
        if (error)
          *error = QObject::tr("Cannot create %1").arg(target);
        return false;
      }
    } else {
      QDir().mkpath(QFileInfo(target).absolutePath());
      QFile::remove(target); // overwrite
      if (!QFile::copy(it.filePath(), target)) {
        if (error)
          *error = QObject::tr("Cannot copy %1").arg(rel);
        return false;
      }
    }
  }
  return true;
}

bool exportProfile(const QString &archive, QString *error) {
  QTemporaryDir staging;
  if (!staging.isValid()) {
    if (error)
      *error = QObject::tr("Cannot create a temporary directory");
    return false;
  }
  // Settings file → staging/whatly.conf
  const QString conf = SettingsManager::instance().settings().fileName();
  if (QFileInfo::exists(conf))
    QFile::copy(conf, staging.filePath(QStringLiteral("whatly.conf")));
  // App data (WebEngine storage, custom css/js) → staging/appdata/
  const QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (QFileInfo::exists(appData) &&
      !copyDirRecursive(appData, staging.filePath(QStringLiteral("appdata")),
                        error))
    return false;
  return makeArchive(staging.path(), archive, error);
}

bool importProfile(const QString &archive, QString *error) {
  QTemporaryDir staging;
  if (!staging.isValid()) {
    if (error)
      *error = QObject::tr("Cannot create a temporary directory");
    return false;
  }
  if (!extractArchive(archive, staging.path(), error))
    return false;
  const QString conf = SettingsManager::instance().settings().fileName();
  const QString stagedConf = staging.filePath(QStringLiteral("whatly.conf"));
  if (QFileInfo::exists(stagedConf)) {
    QDir().mkpath(QFileInfo(conf).absolutePath());
    QFile::remove(conf);
    if (!QFile::copy(stagedConf, conf)) {
      if (error)
        *error = QObject::tr("Could not restore the settings file");
      return false;
    }
  }
  const QString stagedData = staging.filePath(QStringLiteral("appdata"));
  if (QFileInfo::exists(stagedData)) {
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!copyDirRecursive(stagedData, appData, error))
      return false;
  }
  return true;
}

} // namespace Backup
