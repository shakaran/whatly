#include "cloudapi.h"
#include "settingsmanager.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

namespace {
const char kDefaultApiVersion[] = "v21.0";

QString digitsOnly(const QString &s) {
  QString out;
  for (const QChar c : s)
    if (c.isDigit())
      out.append(c);
  return out;
}

QSettings &settings() { return SettingsManager::instance().settings(); }
} // namespace

namespace CloudApi {

QString phoneNumberId() {
  return settings().value(QStringLiteral("cloud/phoneNumberId")).toString();
}
void setPhoneNumberId(const QString &id) {
  settings().setValue(QStringLiteral("cloud/phoneNumberId"), id.trimmed());
}
QString accessToken() {
  return settings().value(QStringLiteral("cloud/accessToken")).toString();
}
void setAccessToken(const QString &token) {
  settings().setValue(QStringLiteral("cloud/accessToken"), token.trimmed());
}
QString apiVersion() {
  const QString v =
      settings().value(QStringLiteral("cloud/apiVersion")).toString().trimmed();
  return v.isEmpty() ? QString::fromLatin1(kDefaultApiVersion) : v;
}
void setApiVersion(const QString &version) {
  settings().setValue(QStringLiteral("cloud/apiVersion"), version.trimmed());
}
bool isConfigured() {
  return !phoneNumberId().isEmpty() && !accessToken().isEmpty();
}

QString messagesUrl(const QString &apiVersion, const QString &phoneNumberId) {
  return QStringLiteral("https://graph.facebook.com/%1/%2/messages")
      .arg(apiVersion, phoneNumberId);
}
QString mediaUrl(const QString &apiVersion, const QString &phoneNumberId) {
  return QStringLiteral("https://graph.facebook.com/%1/%2/media")
      .arg(apiVersion, phoneNumberId);
}

QByteArray textPayload(const QString &to, const QString &body) {
  QJsonObject o;
  o["messaging_product"] = "whatsapp";
  o["recipient_type"] = "individual";
  o["to"] = digitsOnly(to);
  o["type"] = "text";
  o["text"] = QJsonObject{{"body", body}, {"preview_url", true}};
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray mediaPayload(const QString &to, const QString &type,
                        const QString &mediaId, const QString &caption) {
  QJsonObject media{{"id", mediaId}};
  // Only image, video and document accept a caption on the Cloud API.
  if (!caption.isEmpty() &&
      (type == QLatin1String("image") || type == QLatin1String("video") ||
       type == QLatin1String("document")))
    media["caption"] = caption;
  QJsonObject o;
  o["messaging_product"] = "whatsapp";
  o["recipient_type"] = "individual";
  o["to"] = digitsOnly(to);
  o["type"] = type;
  o[type] = media;
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray templatePayload(const QString &to, const QString &templateName,
                           const QString &langCode,
                           const QStringList &bodyParams) {
  QJsonObject tmpl;
  tmpl["name"] = templateName;
  tmpl["language"] = QJsonObject{{"code", langCode}};
  if (!bodyParams.isEmpty()) {
    QJsonArray params;
    for (const QString &p : bodyParams)
      params.append(QJsonObject{{"type", "text"}, {"text", p}});
    tmpl["components"] = QJsonArray{
        QJsonObject{{"type", "body"}, {"parameters", params}}};
  }
  QJsonObject o;
  o["messaging_product"] = "whatsapp";
  o["recipient_type"] = "individual";
  o["to"] = digitsOnly(to);
  o["type"] = "template";
  o["template"] = tmpl;
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString mediaTypeForMime(const QString &mime) {
  if (mime.startsWith(QLatin1String("image/")))
    return QStringLiteral("image");
  if (mime.startsWith(QLatin1String("video/")))
    return QStringLiteral("video");
  if (mime.startsWith(QLatin1String("audio/")))
    return QStringLiteral("audio");
  return QStringLiteral("document");
}

namespace {
// Run one network request to completion (the CLI has no running event loop of
// its own at this point) and return the reply body + status.
struct HttpOut {
  int status = 0;
  QByteArray body;
  QString netError;
};
HttpOut runReply(QNetworkReply *reply) {
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  HttpOut out;
  out.status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  out.body = reply->readAll();
  if (reply->error() != QNetworkReply::NoError)
    out.netError = reply->errorString();
  reply->deleteLater();
  return out;
}

// Turn a Graph API response into a Result (extracts the message id or the API
// error message).
Result resultFromResponse(const HttpOut &http) {
  Result r;
  r.httpStatus = http.status;
  r.response = QString::fromUtf8(http.body);
  const QJsonObject o = QJsonDocument::fromJson(http.body).object();
  if (o.contains(QStringLiteral("error"))) {
    r.error = o.value("error").toObject().value("message").toString(
        QStringLiteral("Cloud API error"));
    return r;
  }
  if (!http.netError.isEmpty() && http.status == 0) {
    r.error = http.netError;
    return r;
  }
  const QJsonArray msgs = o.value("messages").toArray();
  if (!msgs.isEmpty())
    r.messageId = msgs.first().toObject().value("id").toString();
  r.ok = (http.status >= 200 && http.status < 300);
  if (!r.ok && r.error.isEmpty())
    r.error = http.netError.isEmpty()
                  ? QStringLiteral("HTTP %1").arg(http.status)
                  : http.netError;
  return r;
}

Result postJson(const QByteArray &payload) {
  QNetworkAccessManager nam;
  QNetworkRequest req(
      QUrl(messagesUrl(apiVersion(), phoneNumberId())));
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("Authorization",
                   ("Bearer " + accessToken()).toUtf8());
  return resultFromResponse(runReply(nam.post(req, payload)));
}
} // namespace

Result sendText(const QString &to, const QString &body) {
  if (!isConfigured()) {
    Result r;
    r.error = QStringLiteral("Cloud API is not configured (set the phone-number "
                             "id and access token)");
    return r;
  }
  return postJson(textPayload(to, body));
}

Result sendTemplate(const QString &to, const QString &templateName,
                    const QString &langCode, const QStringList &bodyParams) {
  if (!isConfigured()) {
    Result r;
    r.error = QStringLiteral("Cloud API is not configured");
    return r;
  }
  return postJson(templatePayload(to, templateName, langCode, bodyParams));
}

Result sendMediaFile(const QString &to, const QString &filePath,
                     const QString &caption) {
  Result r;
  if (!isConfigured()) {
    r.error = QStringLiteral("Cloud API is not configured");
    return r;
  }
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    r.error = QStringLiteral("cannot read %1").arg(filePath);
    return r;
  }
  const QByteArray bytes = file.readAll();
  file.close();
  const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
  const QString type = mediaTypeForMime(mime);

  // 1) Upload the media, get its id.
  QNetworkAccessManager nam;
  auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
  QHttpPart product;
  product.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QVariant("form-data; name=\"messaging_product\""));
  product.setBody("whatsapp");
  multi->append(product);
  QHttpPart typePart;
  typePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                     QVariant("form-data; name=\"type\""));
  typePart.setBody(mime.toUtf8());
  multi->append(typePart);
  QHttpPart filePart;
  filePart.setHeader(QNetworkRequest::ContentTypeHeader, mime);
  filePart.setHeader(
      QNetworkRequest::ContentDispositionHeader,
      QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                   .arg(QFileInfo(filePath).fileName())));
  filePart.setBody(bytes);
  multi->append(filePart);

  QNetworkRequest upReq(QUrl(mediaUrl(apiVersion(), phoneNumberId())));
  upReq.setRawHeader("Authorization", ("Bearer " + accessToken()).toUtf8());
  QNetworkReply *upReply = nam.post(upReq, multi);
  multi->setParent(upReply);
  const HttpOut up = runReply(upReply);
  const QJsonObject upObj = QJsonDocument::fromJson(up.body).object();
  const QString mediaId = upObj.value("id").toString();
  if (mediaId.isEmpty()) {
    r.httpStatus = up.status;
    r.response = QString::fromUtf8(up.body);
    r.error = upObj.value("error").toObject().value("message").toString(
        up.netError.isEmpty() ? QStringLiteral("media upload failed")
                              : up.netError);
    return r;
  }

  // 2) Send the message referencing the uploaded media id.
  return postJson(mediaPayload(to, type, mediaId, caption));
}

} // namespace CloudApi
