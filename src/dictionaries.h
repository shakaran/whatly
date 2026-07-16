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

// Basenames of the .bdic files in that directory ("en_US", "es_ES", ...) —
// exactly what QWebEngineProfile::setSpellCheckLanguages() expects. Note the
// underscore: the old default was "en-US", with a hyphen, which could not have
// matched a dictionary file even if one had been found.
QStringList availableDictionaries();

// The dictionary to spell-check with: the stored preference while it is still
// installed, else one matching the system locale, else the first available.
QString preferredDictionary();

// The dictionaries to spell-check with, as a list — Chromium checks against all
// of them at once. Comes from the stored "spellCheckLanguages" list, filtered to
// those still installed; falls back to preferredDictionary() when nothing is
// stored, so an upgrade from the single-language setting keeps working.
QStringList selectedDictionaries();

} // namespace Dictionaries

#endif // DICTIONARIES_H
