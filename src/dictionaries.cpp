#include "dictionaries.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace {

// Everywhere a directory of .bdic files can plausibly live, most specific first.
QStringList candidateDirectories() {
  QStringList candidates;

  // An explicit override always wins.
  const QString env = qEnvironmentVariable("QTWEBENGINE_DICTIONARIES_PATH");
  if (!env.isEmpty())
    candidates << env;

  // The dictionaries this build converted. The binary lands in <prefix>/bin and
  // they land in <prefix>/share/whatsie, so derive one from the other instead of
  // baking in an absolute path: this has to work from a build tree, from a
  // ~/.local install and from a system one alike.
  const QString appDir = QCoreApplication::applicationDirPath();
  candidates << appDir + QStringLiteral("/qtwebengine_dictionaries")
             << appDir +
                    QStringLiteral("/../share/whatsie/qtwebengine_dictionaries");

  // Failing that, whatever the distribution happens to ship. Debian and Ubuntu
  // put Chromium's .bdic files here; Qt does not look for them on its own.
  candidates << QStringLiteral("/usr/share/hunspell-bdic")
             << QStringLiteral("/usr/share/qt6/qtwebengine_dictionaries")
             << QStringLiteral("/usr/share/chromium/dictionaries");

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

} // namespace Dictionaries
