#include "aiassistant.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QFile>
#include <QTimer>

static const char kEnabledKey[] = "aiEnabled";
static const char kEndpointKey[] = "aiEndpoint";
static const char kApiKeyKey[] = "aiApiKey";
static const char kModelKey[] = "aiModel";

namespace Ai {

bool isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEnabledKey), false)
      .toBool();
}
QString endpoint() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEndpointKey))
      .toString()
      .trimmed();
}
QString apiKey() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kApiKeyKey))
      .toString()
      .trimmed();
}
QString model() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kModelKey))
      .toString()
      .trimmed();
}
void setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEnabledKey),
                                                  enabled);
}
void setEndpoint(const QString &url) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEndpointKey),
                                                  url.trimmed());
}
void setApiKey(const QString &key) {
  SettingsManager::instance().settings().setValue(QLatin1String(kApiKeyKey),
                                                  key.trimmed());
}
void setModel(const QString &m) {
  SettingsManager::instance().settings().setValue(QLatin1String(kModelKey),
                                                  m.trimmed());
}

QByteArray buildChatRequest(const QString &model, const QString &systemPrompt,
                            const QString &userPrompt) {
  QJsonObject sys{{QStringLiteral("role"), QStringLiteral("system")},
                  {QStringLiteral("content"), systemPrompt}};
  QJsonObject usr{{QStringLiteral("role"), QStringLiteral("user")},
                  {QStringLiteral("content"), userPrompt}};
  QJsonObject o;
  o.insert(QStringLiteral("model"), model);
  o.insert(QStringLiteral("messages"), QJsonArray{sys, usr});
  o.insert(QStringLiteral("temperature"), 0.4);
  o.insert(QStringLiteral("stream"), false);
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString parseChatResponse(const QByteArray &json, QString *error) {
  if (error)
    error->clear();
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error)
      *error = QObject::tr("Unexpected response from the AI service.");
    return QString();
  }
  const QJsonObject o = doc.object();
  // OpenAI-style error: {"error":{"message":"…"}} or {"error":"…"}.
  if (o.contains(QStringLiteral("error"))) {
    const QJsonValue e = o.value(QStringLiteral("error"));
    if (error)
      *error = e.isObject()
                   ? e.toObject().value(QStringLiteral("message")).toString()
                   : e.toString();
    return QString();
  }
  const QJsonArray choices = o.value(QStringLiteral("choices")).toArray();
  if (!choices.isEmpty()) {
    const QJsonObject msg =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = msg.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
      return content.trimmed();
  }
  if (error)
    *error = QObject::tr("The AI service returned no text.");
  return QString();
}

// The system prompts are instructions to the model, not user-facing UI, so they
// are kept in English (and untranslated): they already tell the model to answer
// in the conversation's own language, so the visible output is localised anyway.
QString summarizeSystemPrompt() {
  return QStringLiteral(
      "You summarise a WhatsApp conversation. Reply in the language of the "
      "conversation. Give a short, clear summary of the key points, decisions "
      "and any pending questions or action items. Return only the summary.");
}
QString improveSystemPrompt() {
  return QStringLiteral(
      "You improve a WhatsApp message draft. Fix spelling and grammar and make "
      "it clear and natural, keeping the original meaning, language and tone. "
      "Do not add greetings or explanations. Return only the improved message.");
}
QString suggestReplySystemPrompt() {
  return QStringLiteral(
      "You suggest a reply to a WhatsApp conversation. Reply in the language of "
      "the conversation, in a natural, concise tone that fits the chat. Return "
      "only the suggested reply text, with no quotes or explanation.");
}

QString readContextScript(int maxMessages) {
  const int n = maxMessages < 1 ? 1 : maxMessages;
  return QStringLiteral(
             "(function(){try{"
             "var rows=Array.from(document.querySelectorAll("
             "'#main [role=\"row\"]'));"
             "var out=[];"
             "for(var i=0;i<rows.length;i++){"
             "var r=rows[i];"
             "var pre=r.querySelector('[data-pre-plain-text]');"
             "var t=r.querySelector('.selectable-text');"
             "var who='';"
             "if(pre){var m=/\\]\\s*(.*?):\\s*$/.exec("
             "pre.getAttribute('data-pre-plain-text')||'');"
             "if(m)who=m[1];}"
             "else{who=r.querySelector('[data-icon=\"tail-out\"]')?'Me':'';}"
             "var txt=t?(t.innerText||''):'';"
             "if(txt)out.push((who?who+': ':'')+txt);}"
             "return out.slice(-%1).join('\\n');"
             "}catch(e){return '';}})();")
      .arg(n);
}

long memAvailableMbFromProc(const QByteArray &procMeminfo) {
  // Look for a line "MemAvailable:   12345 kB".
  for (const QByteArray &line : procMeminfo.split('\n')) {
    if (!line.startsWith("MemAvailable:"))
      continue;
    const QByteArray rest = line.mid(QByteArray("MemAvailable:").size()).trimmed();
    bool ok = false;
    const long kb = rest.split(' ').first().toLong(&ok);
    if (ok)
      return kb / 1024; // kB -> MiB
    return -1;
  }
  return -1;
}

long availableMemoryMb() {
#ifdef Q_OS_LINUX
  QFile f(QStringLiteral("/proc/meminfo"));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return -1;
  return memAvailableMbFromProc(f.readAll());
#else
  return -1;
#endif
}

} // namespace Ai

AiClient::AiClient(QObject *parent) : QObject(parent) {}

void AiClient::complete(const QString &systemPrompt, const QString &userPrompt) {
  const QString url = Ai::endpoint();
  if (url.isEmpty()) {
    emit failed(tr("No AI endpoint is configured (Settings → AI assistant)."));
    return;
  }
  if (Ai::model().isEmpty()) {
    emit failed(tr("No AI model is configured (Settings → AI assistant)."));
    return;
  }
  if (userPrompt.trimmed().isEmpty()) {
    emit failed(tr("There is nothing to send to the assistant."));
    return;
  }
  if (!m_net)
    m_net = new QNetworkAccessManager(this);

  QNetworkRequest req{QUrl(url)};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json"));
  const QString key = Ai::apiKey();
  if (!key.isEmpty())
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());

  const QByteArray body =
      Ai::buildChatRequest(Ai::model(), systemPrompt, userPrompt);
  QNetworkReply *reply = m_net->post(req, body);

  // Bound the wait: a hung endpoint (or a local model that never answers) would
  // otherwise leave the request spinning with no resolution. On timeout we abort
  // the reply, which surfaces as a clear failure below.
  auto *timeout = new QTimer(reply);
  timeout->setSingleShot(true);
  connect(timeout, &QTimer::timeout, reply, [reply]() {
    if (reply->isRunning())
      reply->abort();
  });
  timeout->start(180000); // 3 min: local models can be slow

  connect(reply, &QNetworkReply::finished, this, [this, reply, timeout]() {
    timeout->stop();
    reply->deleteLater();
    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError;
    const QByteArray data = reply->readAll();
    QString err;
    const QString out = Ai::parseChatResponse(data, &err);
    if (!out.isEmpty()) {
      emit completed(out);
      return;
    }
    if (aborted)
      err = tr("The assistant took too long and was cancelled.");
    else if (reply->error() != QNetworkReply::NoError && err.isEmpty())
      err = reply->errorString();
    emit failed(err.isEmpty() ? tr("The assistant request failed.") : err);
  });
}
