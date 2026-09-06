#ifndef DICTIONARIES_H
#define DICTIONARIES_H

#include <QString>
#include <QStringList>

// Spell-check dictionaries for Qt WebEngine.
//
// The one thing to know here: Qt WebEngine's spell checker is Chromium's, and
// Chromium reads .bdic — it cannot read hunspell's .dic/.aff at all. This class
// used to scan /usr/share/hunspell for *.dic and hand the basenames to
// setSpellCheckLanguages(), which matched nothing, so the language list came up
// empty and no word was ever underlined. Everything below deals in .bdic only.
namespace Dictionaries {

// The directory Qt WebEngine is pointed at, or empty when there is none.
// Resolve this before the QWebEngineProfile is created: Qt reads
// QTWEBENGINE_DICTIONARIES_PATH once, when the profile is constructed.
QString dictionaryPath();

// A writable per-user dictionaries directory (under AppDataLocation). Users can
// drop extra <language>.bdic files here to add spell-check languages without
// touching a read-only bundle (AppImage/Flatpak/snap). Empty if no data
// location is available.
QString userDictionaryPath();

// Whether this build ships a dictionary of its own for `code`. What sits in the
// user directory cannot answer that: the mirror below is a symlink on Unix and
// a copy on Windows, so there a bundled dictionary and a downloaded one look
// alike. A build that bundles none (the Windows package) always says no.
bool isBundled(const QString &code);

// Make the user directory the one Qt is pointed at: mirror the bundled .bdic
// files into it so they merge with any the user dropped there, and drop
// anything in it that is not a dictionary (an AppImage remounts at a new path
// each launch, which leaves last run's links pointing nowhere). No-op when
// QTWEBENGINE_DICTIONARIES_PATH is set, since then the user is managing the
// directory explicitly. Call before dictionaryPath().
void syncUserDictionaries();

// The mechanism behind syncUserDictionaries(), on explicit directories so it is
// unit tested: drop every *.bdic in userDir that is not a dictionary, then
// mirror every *.bdic from bundledDir that userDir does not already provide (a
// real file the user added keeps its place). bundledDir may be empty (prune
// only).
void syncDictionaryDirs(const QString &userDir, const QString &bundledDir);

// Basenames of the .bdic files in that directory ("en_US", "es_ES", ...) —
// exactly what QWebEngineProfile::setSpellCheckLanguages() expects. Note the
// underscore: the old default was "en-US", with a hyphen, which could not have
// matched a dictionary file even if one had been found.
QStringList availableDictionaries();

// The dictionary to spell-check with: the stored preference while it is still
// installed, else one matching the system locale, else the first available.
QString preferredDictionary();

// The dictionary code to fetch on first run for a system with nothing installed
// (issue #110): the manifest code matching `localeName` (QLocale::system().name(),
// e.g. "es_ES") by full name first, then by bare language, so an "es_AR" system
// still gets Spanish from whatever the manifest carries; empty when the manifest
// offers nothing for that language. Pure, so the first-run choice is unit tested
// without a network. Mirrors preferredDictionary()'s widening, applied to the
// manifest rather than to what is on disk.
QString systemFetchCandidate(const QStringList &manifestCodes,
                             const QString &localeName);

// A short, order-independent tag identifying a manifest by the set of codes it
// carries. Lets a "the system language is not in the manifest" result be
// remembered and re-checked only when the manifest actually changes, rather than
// re-attempted every launch. Pure and unit tested.
QString manifestTag(const QStringList &manifestCodes);

// The dictionaries to spell-check with, as a list — Chromium checks against all
// of them at once. Comes from the stored "spellCheckLanguages" list, filtered to
// those still installed; falls back to preferredDictionary() when nothing is
// stored, so an upgrade from the single-language setting keeps working.
QStringList selectedDictionaries();

// The one chosen language being checked right now, or empty for all of them at
// once. Chromium accepts a word any of its languages knows, so three languages
// at once means three vocabularies' worth of typos going unmarked; narrowing to
// the language actually being written is what makes the underline mean something.
// The chosen set above is untouched by this — it stays the set to switch between.
QString focusedDictionary();
void setFocusedDictionary(const QString &code);

// What QWebEngineProfile::setSpellCheckLanguages() is given: the focused language
// alone while one is focused and still chosen, otherwise every chosen one.
QStringList activeDictionaries();

// The next language to focus after `current`, for a key pressed mid-sentence:
// each of `chosen` in turn and then all of them together (an empty string) once
// per lap. An unknown `current` starts the lap over. Pure and unit tested.
QString nextFocus(const QStringList &chosen, const QString &current);

// A language code as something to read: "Esperanto (eo)", "British English
// (en_GB)". Named by the code's own locale, so the territory is not lost — the
// language alone would have every English dictionary reading "American English".
// A code no QLocale recognises is handed back as it stands.
QString languageLabel(const QString &code);

} // namespace Dictionaries

#endif // DICTIONARIES_H
