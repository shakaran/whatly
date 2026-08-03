#include "ollama.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <memory>

namespace Ollama {

QList<RecModel> recommendedModels() {
  return {
      {QStringLiteral("qwen2.5:3b"), QStringLiteral("~1.9 GB"),
       QObject::tr("Good quality, light and fast. Recommended.")},
      {QStringLiteral("llama3.2:3b"), QStringLiteral("~2.0 GB"),
       QObject::tr("Meta Llama, balanced and multilingual.")},
      {QStringLiteral("gemma2:2b"), QStringLiteral("~1.6 GB"),
       QObject::tr("Google Gemma, very small.")},
      {QStringLiteral("llama3.2:1b"), QStringLiteral("~1.3 GB"),
       QObject::tr("Tiny and fastest; lower quality.")},
      {QStringLiteral("phi3:mini"), QStringLiteral("~2.2 GB"),
       QObject::tr("Microsoft Phi-3, strong for its size.")},
  };
}

bool isLocalEndpoint(const QString &endpoint) {
  const QString host = QUrl(endpoint.trimmed()).host().toLower();
  return host == QLatin1String("localhost") ||
         host == QLatin1String("127.0.0.1") || host == QLatin1String("::1");
}

QString baseUrl(const QString &endpoint) {
  const QUrl u(endpoint.trimmed());
  if (u.isValid() && !u.host().isEmpty()) {
    QString base = u.scheme().isEmpty() ? QStringLiteral("http") : u.scheme();
    base += QStringLiteral("://") + u.host();
    if (u.port() > 0)
      base += QLatin1Char(':') + QString::number(u.port());
    return base;
  }
  return QStringLiteral("http://localhost:11434");
}

QStringList parseInstalledModels(const QByteArray &tagsJson) {
  QStringList out;
  const QJsonDocument doc = QJsonDocument::fromJson(tagsJson);
  if (!doc.isObject())
    return out;
  const QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();
  for (const QJsonValue &m : models) {
    const QString name = m.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty())
      out << name;
  }
  return out;
}

int parsePullProgress(const QByteArray &jsonLine, QString *status) {
  if (status)
    status->clear();
  const QJsonDocument doc = QJsonDocument::fromJson(jsonLine.trimmed());
  if (!doc.isObject())
    return -1;
  const QJsonObject o = doc.object();
  if (status)
    *status = o.value(QStringLiteral("status")).toString();
  const double total = o.value(QStringLiteral("total")).toDouble(0);
  const double completed = o.value(QStringLiteral("completed")).toDouble(0);
  if (total > 0)
    return qBound(0, static_cast<int>(completed * 100.0 / total), 100);
  return -1;
}

} // namespace Ollama

OllamaManager::OllamaManager(QObject *parent) : QObject(parent) {}

void OllamaManager::check(const QString &base) {
  if (!m_net)
    m_net = new QNetworkAccessManager(this);
  QNetworkRequest req{QUrl(base + QStringLiteral("/api/tags"))};
  QNetworkReply *reply = m_net->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit checked(false, {});
      return;
    }
    emit checked(true, Ollama::parseInstalledModels(reply->readAll()));
  });
}

void OllamaManager::pull(const QString &base, const QString &model) {
  if (!m_net)
    m_net = new QNetworkAccessManager(this);
  QNetworkRequest req{QUrl(base + QStringLiteral("/api/pull"))};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json"));
  QJsonObject body{{QStringLiteral("name"), model},
                   {QStringLiteral("stream"), true}};
  QNetworkReply *reply =
      m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

  // The response is a stream of JSON lines; parse each as it arrives so the UI
  // can show live progress rather than freezing until the (large) download ends.
  auto buffer = std::make_shared<QByteArray>();
  connect(reply, &QNetworkReply::readyRead, this, [this, reply, buffer]() {
    buffer->append(reply->readAll());
    int nl;
    while ((nl = buffer->indexOf('\n')) >= 0) {
      const QByteArray line = buffer->left(nl);
      buffer->remove(0, nl + 1);
      if (line.trimmed().isEmpty())
        continue;
      QString status;
      const int pct = Ollama::parsePullProgress(line, &status);
      emit pullProgress(pct, status);
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit pullFinished(false, reply->errorString());
      return;
    }
    // Ollama reports a final {"status":"success"}; treat a clean finish as ok.
    emit pullFinished(true, QString());
  });
}
