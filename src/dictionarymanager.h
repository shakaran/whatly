#ifndef DICTIONARYMANAGER_H
#define DICTIONARYMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// One spell-check dictionary as the catalogue knows it. `code` is the .bdic
// basename QWebEngineProfile::setSpellCheckLanguages() expects ("en_US"); size
// and sha256 come from the published manifest and gate a download as valid.
struct DictionaryEntry {
  QString code;
  qint64 size = 0;
  QString sha256; // lower-case hex of the .bdic
};

// Downloadable spell-check dictionaries (issue #46). The full set is ~45 MB and
// almost nobody needs all of it, so the packages bundle a small minimum and the
// rest are fetched on demand into the writable user dictionary directory
// (Dictionaries::userDictionaryPath()), from GitHub release assets — the same
// host and fetch pattern updatechecker already uses.
//
// A downloaded file is verified (size + SHA-256 from the manifest) before it is
// kept, because Chromium parses the .bdic and a truncated or wrong file must
// never reach the engine.
class DictionaryManager : public QObject {
  Q_OBJECT
public:
  explicit DictionaryManager(QObject *parent = nullptr);

  // Base URL the manifest and the per-language assets live under. Overridable so
  // tests can point at a local server; defaults to the stable "dictionaries"
  // release tag so the URLs do not move with the app version.
  static QString catalogBaseUrl();
  static QString manifestUrl();
  static QString assetUrl(const QString &code);

  // Human-readable name for a dictionary code, derived from the locale rather
  // than a hand-kept table: "de_DE" -> "Deutsch (Deutschland)". Falls back to
  // the code when the locale is not recognised (e.g. "eo").
  static QString displayName(const QString &code);

  // Parse the published manifest JSON. Tolerant of unknown fields; skips entries
  // without a code. Pure and side-effect-free, so it is unit-tested directly.
  static QList<DictionaryEntry> parseManifest(const QByteArray &json);

  // True when `data` is exactly `expectedSize` bytes and its SHA-256 matches
  // `expectedSha256` (case-insensitive). An empty expectedSha256 means "cannot
  // verify" and returns false: an unverifiable download is not trusted.
  static bool verify(const QByteArray &data, qint64 expectedSize,
                     const QString &expectedSha256);

  // Codes currently present (bundled or already downloaded).
  static QStringList installed();

  // Fetch the catalogue; emits catalogReady or catalogFailed.
  void fetchCatalog();

  // Download `entry` into the user dictionary directory, verifying it before it
  // is put in place. Safe to call for a code already downloading (ignored).
  void download(const DictionaryEntry &entry);

  // Remove a downloaded dictionary. Returns false if it is not a real file in
  // the user directory (e.g. a bundled one, which would just relink next launch).
  bool remove(const QString &code);

  // Whether `code` is a real downloaded file in the user directory (so remove()
  // would succeed), as opposed to a bundled dictionary mirrored as a symlink.
  static bool isRemovable(const QString &code);

  bool isDownloading(const QString &code) const;

signals:
  void catalogReady(const QList<DictionaryEntry> &entries);
  void catalogFailed(const QString &error);
  void downloadProgress(const QString &code, int percent);
  void downloadFinished(const QString &code, bool ok, const QString &error);

private:
  QNetworkAccessManager *m_net = nullptr;
  QStringList m_downloading;
};

#endif // DICTIONARYMANAGER_H
