#ifndef DICTIONARYBOOTSTRAP_H
#define DICTIONARYBOOTSTRAP_H

#include <QObject>
#include <QString>
#include <QStringList>

class DictionaryManager;
class QNetworkAccessManager;

// First-run spell-check dictionary fetch (issue #110). When a fresh install
// carries no dictionary of its own, fetch the one matching the system locale
// from the "dictionaries" release, so a non-English user gets their own language
// automatically instead of a bundled en_US they never chose, and the packages
// can ship none at all.
//
// It acts once and then gets out of the way. The stop conditions are the actual
// design, each a settings key under "dictionaries/":
//   - the user removed a dictionary in Settings (systemFetchOptOut): a choice,
//     not to be undone behind their back;
//   - the system language is not in the manifest, remembered against the
//     manifest's own identity (systemFetchNotInManifest / systemFetchManifestTag)
//     so it is not re-attempted or re-logged every launch, only re-checked when
//     the manifest itself changes;
//   - a bounded number of attempts per session for a connection still coming up,
//     then nothing until the next launch. Deliberately not a timer that runs for
//     ever: a blocked fetch can never succeed, and a fixed endpoint hit every
//     minute is pointless traffic and, on a managed machine, hard to tell from
//     beaconing.
class DictionaryBootstrap : public QObject {
  Q_OBJECT
public:
  explicit DictionaryBootstrap(QObject *parent = nullptr);

  // Whether a first-run fetch should even be considered: nothing installed and
  // the user has not opted out. Reads settings only, so it is cheap and testable.
  static bool shouldAttempt();

  // Kick off the one attempt and its bounded backoff. A no-op (emitting
  // finished(false, {})) when shouldAttempt() is false. Calling it again while a
  // run is in flight is ignored.
  void start();

  // Run the fetch pipeline now, ignoring the installed()/opt-out gate that
  // start() applies. start() is "gate, then this"; tests drive this directly so
  // the network path is covered without depending on what the host already has
  // installed. Ignored while a run is in flight.
  void beginFetch();

signals:
  // Reached a terminal state, for logging and tests. `code` is the dictionary
  // fetched, or empty when nothing was (opted out, not in the manifest, or the
  // network never came up within the session's attempts).
  void finished(bool fetched, const QString &code);

private:
  void attemptManifest();
  void onManifestBody(const QByteArray &body);
  void recordNotInManifest(const QStringList &codes, const QString &locale);

  DictionaryManager *m_dict = nullptr;
  QNetworkAccessManager *m_net = nullptr;
  int m_attempts = 0;
  bool m_running = false;
};

#endif // DICTIONARYBOOTSTRAP_H
