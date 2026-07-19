#ifndef CUSTOMJS_H
#define CUSTOMJS_H

#include <QList>
#include <QString>

class QWebEngineProfile;

// A small manager for user JavaScript "addons": each addon is a .js file the
// user supplies, stored per-account, that Whatly injects into WhatsApp Web after
// the page is ready. Addons can be added, removed, and individually enabled or
// disabled. Every enabled addon runs in its own try/catch so a broken one can
// never take the page (or the other addons) down.
//
// Storage: the .js files live under <AppData>/jsaddons<profile-suffix>/, so each
// account has its own set. The enabled/disabled state is kept in the account's
// settings, keyed by addon name.
namespace CustomJs {

struct Addon {
  QString name;    // sanitised, unique; also the file's base name
  bool enabled;    // whether it is injected
};

// All addons known for the current account, sorted by name.
QList<Addon> addons();

// The raw source of one addon (empty if it does not exist).
QString sourceOf(const QString &name);

// Import a .js file as a new addon. The addon name is derived from `path`'s base
// name (sanitised); pass `name` to override it. On success the addon is enabled.
// Returns the stored addon name, or an empty string on failure (with *error).
QString addFromFile(const QString &path, QString *error,
                    const QString &name = QString());

// Remove an addon (its file and its stored state).
void remove(const QString &name);

// Enable or disable an addon.
void setEnabled(const QString &name, bool enabled);
bool isEnabled(const QString &name);

// True when at least one addon is enabled (and non-empty).
bool isActive();

// The combined injected script: every enabled addon wrapped in its own guard.
QString scriptSource();

// (Re)install the combined addon script on a profile; removes it when inactive.
void install(QWebEngineProfile *profile);

} // namespace CustomJs

#endif // CUSTOMJS_H
