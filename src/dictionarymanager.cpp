#include "dictionarymanager.h"
#include "dictionaries.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace {
// A dedicated release tag, not a versioned one: the dictionaries outlive any
// single app release, so their URLs must not move when the app is re-tagged.
const QString kDefaultBase = QStringLiteral(
    "https://github.com/shakaran/whatly/releases/download/dictionaries");
} // namespace

DictionaryManager::DictionaryManager(QObject *parent) : QObject(parent) {}

QString DictionaryManager::catalogBaseUrl() {
  const QString env = qEnvironmentVariable("WHATLY_DICT_BASE_URL");
  return env.isEmpty() ? kDefaultBase : env;
}

QString DictionaryManager::manifestUrl() {
  return catalogBaseUrl() + QStringLiteral("/manifest.json");
}

QString DictionaryManager::assetUrl(const QString &code) {
  return catalogBaseUrl() + QLatin1Char('/') + code + QStringLiteral(".bdic");
}

QString DictionaryManager::displayName(const QString &code) {
  QLocale loc(code);
  // QLocale maps an unknown code to the C locale; treat that as "not recognised"
  // and show the raw code (e.g. "eo" on Qt builds without Esperanto).
  if (loc.language() == QLocale::C || loc.language() == QLocale::AnyLanguage)
    return code;
  const QString lang = loc.nativeLanguageName();
  if (lang.isEmpty())
    return code;
  const QString terr = loc.nativeTerritoryName();
  return terr.isEmpty() ? lang
                        : QStringLiteral("%1 (%2)").arg(lang, terr);
}

QList<DictionaryEntry> DictionaryManager::parseManifest(const QByteArray &json) {
  QList<DictionaryEntry> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return out;
  const QJsonArray arr = doc.object().value(QStringLiteral("dictionaries")).toArray();
  for (const QJsonValue &v : arr) {
    const QJsonObject o = v.toObject();
    DictionaryEntry e;
    e.code = o.value(QStringLiteral("code")).toString();
    if (e.code.isEmpty())
      continue; // an entry with no code is unusable
    e.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
    e.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();
    out.append(e);
  }
  return out;
}

bool DictionaryManager::verify(const QByteArray &data, qint64 expectedSize,
                               const QString &expectedSha256) {
  if (expectedSha256.isEmpty())
    return false; // unverifiable is not trusted
  if (expectedSize > 0 && data.size() != expectedSize)
    return false;
  const QByteArray got =
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
  return got.compare(expectedSha256.toLatin1(), Qt::CaseInsensitive) == 0;
}

QStringList DictionaryManager::installed() {
  return Dictionaries::availableDictionaries();
}

bool DictionaryManager::isDownloading(const QString &code) const {
  return m_downloading.contains(code);
}

void DictionaryManager::fetchCatalog() {
  if (!m_net)
    m_net = new QNetworkAccessManager(this);
  QNetworkRequest req{QUrl(manifestUrl())};
  req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Whatly"));
  QNetworkReply *reply = m_net->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit catalogFailed(reply->errorString());
      return;
    }
    emit catalogReady(parseManifest(reply->readAll()));
  });
}

void DictionaryManager::download(const DictionaryEntry &entry) {
  if (entry.code.isEmpty() || isDownloading(entry.code))
    return;
  const QString dir = Dictionaries::userDictionaryPath();
  if (dir.isEmpty()) {
    emit downloadFinished(entry.code, false,
                          tr("No writable dictionary directory."));
    return;
  }
  if (!m_net)
    m_net = new QNetworkAccessManager(this);
  m_downloading << entry.code;

  QNetworkRequest req{QUrl(assetUrl(entry.code))};
  req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Whatly"));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy); // release assets 302
  QNetworkReply *reply = m_net->get(req);

  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, code = entry.code](qint64 received, qint64 total) {
            if (total > 0)
              emit downloadProgress(code, int(received * 100 / total));
          });

  connect(reply, &QNetworkReply::finished, this, [this, reply, entry, dir]() {
    reply->deleteLater();
    m_downloading.removeAll(entry.code);
    if (reply->error() != QNetworkReply::NoError) {
      emit downloadFinished(entry.code, false, reply->errorString());
      return;
    }
    const QByteArray data = reply->readAll();
    if (!verify(data, entry.size, entry.sha256)) {
      emit downloadFinished(
          entry.code, false,
          tr("The downloaded dictionary failed verification."));
      return;
    }
    QDir().mkpath(dir);
    // Atomic: write to a temp and rename, so a half-written .bdic is never
    // visible to the engine. QSaveFile does exactly that on commit().
    QSaveFile f(QDir(dir).filePath(entry.code + QStringLiteral(".bdic")));
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() ||
        !f.commit()) {
      emit downloadFinished(entry.code, false,
                            tr("Could not save the dictionary."));
      return;
    }
    emit downloadFinished(entry.code, true, QString());
  });
}

bool DictionaryManager::isRemovable(const QString &code) {
  const QString dir = Dictionaries::userDictionaryPath();
  if (dir.isEmpty())
    return false;
  const QFileInfo fi(QDir(dir).filePath(code + QStringLiteral(".bdic")));
  return fi.exists() && !fi.isSymLink();
}

bool DictionaryManager::remove(const QString &code) {
  const QString dir = Dictionaries::userDictionaryPath();
  if (dir.isEmpty())
    return false;
  const QString path = QDir(dir).filePath(code + QStringLiteral(".bdic"));
  const QFileInfo fi(path);
  // Only remove a real downloaded file. A symlink is a bundled dictionary
  // mirrored by syncUserDictionaries(); deleting it would just relink next
  // launch, so report that it cannot be removed.
  if (!fi.exists() || fi.isSymLink())
    return false;
  return QFile::remove(path);
}
