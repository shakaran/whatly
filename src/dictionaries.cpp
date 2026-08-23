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

bool isBundled(const QString &code) {
  const QString dir = bundledSourceDir();
  if (dir.isEmpty())
    return false;
  return QFileInfo::exists(QDir(dir).filePath(code + QStringLiteral(".bdic")));
}

void syncDictionaryDirs(const QString &userDir, const QString &bundledDir) {
  if (userDir.isEmpty())
    return;
  QDir().mkpath(userDir);
  QDir ud(userDir);

  // Drop whatever is not a dictionary first. Two things leave one behind. An
  // AppImage remounts at a fresh path each launch, so last run's symlinks into
  // the bundle no longer resolve. And on Windows QFile::link() writes a .lnk
  // shortcut, which Qt does not read back as a symlink because it decides that
  // by file name — so a shortcut called da_DK.bdic answered no to the old
  // isSymLink() test, survived every launch, and was handed to Chromium as a
  // dictionary it cannot read: the language showed as installed and silently
  // checked nothing. Asking the file what it is answers both, and a half-written
  // download besides. Chromium's .bdic begins with "BDic". QDir::System is what
  // makes the listing include a symlink whose target is gone, and such a link
  // cannot be opened — which is the same answer.
  const auto entries = ud.entryInfoList(
      {QStringLiteral("*.bdic")},
      QDir::Files | QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : entries) {
    bool isDictionary = false;
    {
      QFile f(fi.absoluteFilePath());
      isDictionary = f.open(QIODevice::ReadOnly) &&
                     f.read(4) == QByteArrayLiteral("BDic");
    }
    if (!isDictionary)
      QFile::remove(fi.absoluteFilePath());
  }

  if (bundledDir.isEmpty() ||
      QDir(bundledDir).canonicalPath() == ud.canonicalPath())
    return; // nothing to mirror, or the user dir already is the bundle

  // Mirror each bundled dictionary the user dir does not already provide. A
  // real .bdic the user dropped in keeps its place and shadows the bundled one
  // of the same name; a still-valid mirror from a previous run is left as is.
  const QStringList bundled =
      QDir(bundledDir).entryList({QStringLiteral("*.bdic")}, QDir::Files);
  for (const QString &f : bundled) {
    const QString dest = ud.filePath(f);
    if (QFileInfo::exists(dest))
      continue;
#ifdef Q_OS_WIN
    // Copy, because QFile::link() cannot make a link here: Qt's own
    // documentation says the name must end in .lnk for that, and this one ends
    // in .bdic. The bundle is read-only either way, and a copy is the only form
    // the engine can open.
    QFile::copy(QDir(bundledDir).filePath(f), dest);
#else
    QFile::link(QDir(bundledDir).filePath(f), dest);
#endif
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
  // An unrecognised code maps to the C locale (or to no language at all): show it
  // as it stands rather than inventing a name for it.
  if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage)
    return code;
  // Ask the dictionary's own locale, not QLocale(its language). The language-only
  // form throws the territory away and Qt fills in the likeliest one, which for
  // English is the United States — so en_GB and en_AU both came out as "American
  // English". Asking en_GB itself gives "British English".
  QString name = locale.nativeLanguageName();
  if (name.isEmpty())
    return code;

  // CLDR names a variant that is not its default by qualifying the plain name —
  // "português europeu" beside the plain "português" that means Brazil — and this
  // list says the territory itself, so the qualifier only repeats it in other
  // words. Cut it back to the plain name. A name that differs outright rather than
  // by a qualifier ("British English" beside "American English") does not start
  // with it and stays whole.
  const QString plain = QLocale(locale.language()).nativeLanguageName();
  if (!plain.isEmpty() && name.size() > plain.size() && name.startsWith(plain))
    name = plain;

  // Two cases where the code itself is the only honest thing to add. A code Qt does
  // not model exactly: the bundle ships de_DE beside de_DE_neu (the reformed
  // spelling) and en_US beside en-US, and Qt reads each pair as one and the same
  // locale, so naming them by locale alone would put two entries with identical
  // names in the picker. And a bare language like "eo", where Qt would otherwise
  // supply whichever territory it considers likeliest — for Esperanto, "mondo".
  if (locale.name() != code) {
    // One of those is worth spelling out rather than dumping the code: a variant
    // tag, as in de_DE_neu — German in the reformed spelling, which Qt reads as
    // plain de_DE. Name the territory and the variant, because "Deutsch
    // (de_DE_neu)" tells a reader nothing about what it is.
    const QStringList parts = code.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    const QString territory = locale.nativeTerritoryName();
    if (parts.size() > 2 && !territory.isEmpty())
      return name + QStringLiteral(" (") + territory +
             QStringLiteral(" · ") + parts.last() + QStringLiteral(")");
    return name + QStringLiteral(" (") + code + QStringLiteral(")");
  }

  // Every entry gets the same shape, "language (territory)", because a list where
  // some rows carry a territory and others do not reads as an accident. CLDR
  // qualifies only the variants that are not its default, so plain "português" IS
  // Brazilian and plain "español" IS Argentinian while es_ES and pt_PT carry theirs
  // — naming the territory is what makes each row say which one it is.
  const QString territory = locale.nativeTerritoryName();
  if (territory.isEmpty())
    return name;

  // Where the name already ends in the territory, take it out before adding it
  // back as the qualifier: "español de España" would otherwise become "español de
  // España (España)", which is the row that stood out in the first place. The
  // connector left behind ("de", "do", "of") is one short word, so drop it too.
  const int at = name.lastIndexOf(territory, -1, Qt::CaseInsensitive);
  if (at > 0) {
    name.truncate(at);
    name = name.trimmed();
    const int space = name.lastIndexOf(QLatin1Char(' '));
    if (space > 0 && name.length() - space - 1 <= 3)
      name.truncate(space);
  }
  return name + QStringLiteral(" (") + territory + QStringLiteral(")");
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
