#include "cloudwebhook.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QUrlQuery>

namespace {
QSettings &settings() { return SettingsManager::instance().settings(); }
} // namespace

namespace CloudWebhook {

bool isEnabled() {
  return settings().value(QStringLiteral("cloudwebhook/enabled"), false).toBool();
}
void setEnabled(bool on) {
  settings().setValue(QStringLiteral("cloudwebhook/enabled"), on);
}
QString verifyToken() {
  return settings().value(QStringLiteral("cloudwebhook/verifyToken")).toString();
}
void setVerifyToken(const QString &token) {
  settings().setValue(QStringLiteral("cloudwebhook/verifyToken"), token.trimmed());
}
QString appSecret() {
  return settings().value(QStringLiteral("cloudwebhook/appSecret")).toString();
}
void setAppSecret(const QString &secret) {
  settings().setValue(QStringLiteral("cloudwebhook/appSecret"), secret.trimmed());
}

QMap<QString, QString> parseQuery(const QString &path) {
  QMap<QString, QString> out;
  const int q = path.indexOf('?');
  if (q < 0)
    return out;
  const QUrlQuery query(path.mid(q + 1));
  const auto items = query.queryItems(QUrl::FullyDecoded);
  for (const auto &kv : items)
    out.insert(kv.first, kv.second);
  return out;
}

QString verifyChallenge(const QMap<QString, QString> &query,
                        const QString &expectedToken) {
  if (expectedToken.isEmpty())
    return QString();
  if (query.value(QStringLiteral("hub.mode")) != QLatin1String("subscribe"))
    return QString();
  if (query.value(QStringLiteral("hub.verify_token")) != expectedToken)
    return QString();
  return query.value(QStringLiteral("hub.challenge"));
}

bool verifySignature(const QByteArray &body, const QString &signatureHeader,
                     const QString &appSecret) {
  if (appSecret.isEmpty())
    return true; // signature checking not configured yet
  const QString prefix = QStringLiteral("sha256=");
  if (!signatureHeader.startsWith(prefix))
    return false;
  const QByteArray provided = signatureHeader.mid(prefix.size()).toLatin1();
  const QByteArray expected =
      QMessageAuthenticationCode::hash(body, appSecret.toUtf8(),
                                       QCryptographicHash::Sha256)
          .toHex();
  // Constant-time-ish compare (lengths equal for hex of a fixed-size digest).
  if (provided.size() != expected.size())
    return false;
  int diff = 0;
  for (int i = 0; i < expected.size(); ++i)
    diff |= (provided.at(i) ^ expected.at(i));
  return diff == 0;
}

QList<Incoming> parseIncoming(const QByteArray &body) {
  QList<Incoming> out;
  const QJsonDocument doc = QJsonDocument::fromJson(body);
  if (!doc.isObject())
    return out;
  const QJsonArray entries = doc.object().value(QStringLiteral("entry")).toArray();
  for (const QJsonValue &ev : entries) {
    const QJsonArray changes =
        ev.toObject().value(QStringLiteral("changes")).toArray();
    for (const QJsonValue &cv : changes) {
      const QJsonObject value =
          cv.toObject().value(QStringLiteral("value")).toObject();
      const QJsonArray messages =
          value.value(QStringLiteral("messages")).toArray();
      for (const QJsonValue &mv : messages) {
        const QJsonObject m = mv.toObject();
        Incoming in;
        in.from = m.value(QStringLiteral("from")).toString();
        in.id = m.value(QStringLiteral("id")).toString();
        in.type = m.value(QStringLiteral("type")).toString();
        if (in.type == QLatin1String("text"))
          in.text = m.value(QStringLiteral("text"))
                        .toObject()
                        .value(QStringLiteral("body"))
                        .toString();
        out.append(in);
      }
    }
  }
  return out;
}

} // namespace CloudWebhook
