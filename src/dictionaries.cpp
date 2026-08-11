#include "dictionaries.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

namespace {

// The read-only places a bundled directory of .bdic files can live, most
// specific first. Excludes the env override and the writable user directory, so
// syncUserDictionaries() can treat it purely as the source to mirror from.
QStringList bundledCandidateDirectories() {
  QStringList candidates;
  // The dictionaries this build converted. The binary lands in <prefix>/bin and
  // they land in <prefix>/share/whatly, so derive one from the other instead of
  // baking in an absolute path: this has to work from a build tree, from a
  // ~/.local install and from a system one alike.
  const QString appDir = QCoreApplication::applicationDirPath();
  candidates << appDir + QStringLiteral("/qtwebengine_dictionaries")
             << appDir +
                    QStringLiteral("/../share/whatly/qtwebengine_dictionaries");

  // Failing that, whatever the distribution happens to ship. Debian and Ubuntu
  // put Chromium's .bdic files here; Qt does not look for them on its own.
  candidates << QStringLiteral("/usr/share/hunspell-bdic")
             << QStringLiteral("/usr/share/qt6/qtwebengine_dictionaries")
             << QStringLiteral("/usr/share/chromium/dictionaries");
  return candidates;
}

// The first bundled directory that actually holds .bdic files, canonicalised.
QString bundledSourceDir() {
  for (const QString &candidate : bundledCandidateDirectories()) {
    QDir dir(candidate);
    if (dir.exists() &&
        !dir.entryList({QStringLiteral("*.bdic")}, QDir::Files).isEmpty())
      return dir.canonicalPath();
  }
  return QString();
}

QStringList candidateDirectories() {
  QStringList candidates;

  // An explicit override always wins.
  const QString env = qEnvironmentVariable("QTWEBENGINE_DICTIONARIES_PATH");
  if (!env.isEmpty())
    candidates << env;

  // The writable user directory next: syncUserDictionaries() fills it with the
  // bundled dictionaries plus any the user dropped in, so it is the merged set.
  const QString userDir = Dictionaries::userDictionaryPath();
  if (!userDir.isEmpty())
    candidates << userDir;

  candidates << bundledCandidateDirectories();
  return candidates;
}

} // namespace

namespace Dictionaries {

QString dictionaryPath() {
  for (const QString &candidate : candidateDirectories()) {
    QDir dir(candidate);
    // A directory with no .bdic in it is no use, so keep looking rather than
    // settling on it — which is how the old code ended up on /usr/share/hunspell
    // and found nothing Qt could read.
    if (dir.exists() &&
        !dir.entryList({QStringLiteral("*.bdic")}, QDir::Files).isEmpty())
      return dir.canonicalPath();
  }
  return QString();
}

QString userDictionaryPath() {
  const QString base =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty())
    return QString();
  return base + QStringLiteral("/qtwebengine_dictionaries");
}

void syncDictionaryDirs(const QString &userDir, const QString &bundledDir) {
  if (userDir.isEmpty())
    return;
  QDir().mkpath(userDir);
  QDir ud(userDir);

  // Drop stale links first: an AppImage remounts at a fresh path each launch, so
  // last run's symlinks into the bundle no longer resolve. QDir::System is what
  // makes a listing include a symlink whose target is gone.
  const auto entries = ud.entryInfoList(
      {QStringLiteral("*.bdic")},
      QDir::Files | QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : entries) {
    if (fi.isSymLink() && !QFileInfo::exists(fi.symLinkTarget()))
      QFile::remove(fi.absoluteFilePath());
  }

  if (bundledDir.isEmpty() ||
      QDir(bundledDir).canonicalPath() == ud.canonicalPath())
    return; // nothing to mirror, or the user dir already is the bundle

  // Symlink each bundled dictionary the user dir does not already provide. A
  // real .bdic the user dropped in keeps its place and shadows the bundled one
  // of the same name; a still-valid link from a previous run is left as is.
  const QStringList bundled =
      QDir(bundledDir).entryList({QStringLiteral("*.bdic")}, QDir::Files);
  for (const QString &f : bundled) {
    const QString dest = ud.filePath(f);
    if (!QFileInfo::exists(dest))
      QFile::link(QDir(bundledDir).filePath(f), dest);
  }
}

void syncUserDictionaries() {
  // An explicit override means the user manages that directory themselves.
  if (!qEnvironmentVariable("QTWEBENGINE_DICTIONARIES_PATH").isEmpty())
    return;
  syncDictionaryDirs(userDictionaryPath(), bundledSourceDir());
}

QStringList availableDictionaries() {
  const QString path = dictionaryPath();
  if (path.isEmpty())
    return {};

  QStringList names;
  const QStringList files =
      QDir(path).entryList({QStringLiteral("*.bdic")}, QDir::Files);
  for (const QString &file : files)
    names << QFileInfo(file).completeBaseName();

  names.removeDuplicates();
  names.sort();
  return names;
}

QString preferredDictionary() {
  const QStringList available = availableDictionaries();
  if (available.isEmpty())
    return QString();

  const QString stored = SettingsManager::instance()
                             .settings()
                             .value(QStringLiteral("spellCheckLanguage"))
                             .toString();
  if (available.contains(stored))
    return stored;

  // Nothing chosen yet, or what was chosen is gone: follow the system locale by
  // full name first (es_ES), then by language, so an es_AR system still gets
  // Spanish rather than whatever sorts first.
  const QString locale = QLocale::system().name();
  if (available.contains(locale))
    return locale;

  const QString language = locale.section(QLatin1Char('_'), 0, 0);
  for (const QString &candidate : available)
    if (candidate == language ||
        candidate.startsWith(language + QLatin1Char('_')))
      return candidate;

  return available.first();
}

QStringList selectedDictionaries() {
  const QStringList available = availableDictionaries();
  if (available.isEmpty())
    return {};

  const QStringList stored = SettingsManager::instance()
                                 .settings()
                                 .value(QStringLiteral("spellCheckLanguages"))
                                 .toStringList();
  QStringList chosen;
  for (const QString &d : stored)
    if (available.contains(d)) // drop any that were uninstalled since
      chosen << d;

  // No multi-language list yet (fresh install or upgrade from the single
  // setting): fall back to the one preferred dictionary.
  if (chosen.isEmpty()) {
    const QString one = preferredDictionary();
    if (!one.isEmpty())
      chosen << one;
  }
  return chosen;
}

QString focusedDictionary() {
  const QString stored = SettingsManager::instance()
                             .settings()
                             .value(QStringLiteral("spellCheckFocus"))
                             .toString();
  // A language dropped from the picker (or uninstalled) leaves no focus behind,
  // rather than silently checking against nothing.
  return selectedDictionaries().contains(stored) ? stored : QString();
}

void setFocusedDictionary(const QString &code) {
  SettingsManager::instance().settings().setValue(
      QStringLiteral("spellCheckFocus"), code);
}

QStringList activeDictionaries() {
  const QString focus = focusedDictionary();
  return focus.isEmpty() ? selectedDictionaries() : QStringList{focus};
}

QString languageLabel(const QString &code) {
  const QLocale locale(code);
  if (locale.language() == QLocale::C)
    return code;
  return QLocale(locale.language()).nativeLanguageName() +
         QStringLiteral(" (") + code + QStringLiteral(")");
}

QString nextFocus(const QStringList &chosen, const QString &current) {
  if (chosen.size() < 2)
    return QString(); // nothing to switch between: all of them is the only stop
  const int at = chosen.indexOf(current);
  if (at < 0)
    return chosen.first(); // came from all of them, or from one since removed
  return at + 1 < chosen.size() ? chosen.at(at + 1) : QString();
}

} // namespace Dictionaries
