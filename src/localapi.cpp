#include "localapi.h"
#include "cloudwebhook.h"
#include "settingsmanager.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
QSettings &settings() { return SettingsManager::instance().settings(); }
} // namespace

namespace LocalApi {

bool isEnabled() {
  return settings().value(QStringLiteral("localapi/enabled"), false).toBool();
}
void setEnabled(bool on) {
  settings().setValue(QStringLiteral("localapi/enabled"), on);
}
int port() {
  const int p = settings().value(QStringLiteral("localapi/port"), kDefaultPort)
                    .toInt();
  return (p > 0 && p <= 65535) ? p : kDefaultPort;
}
void setPort(int p) {
  settings().setValue(QStringLiteral("localapi/port"), p);
}
QString token() {
  return settings().value(QStringLiteral("localapi/token")).toString();
}
void setToken(const QString &t) {
  settings().setValue(QStringLiteral("localapi/token"), t.trimmed());
}
bool isConfigured() { return isEnabled() && !token().isEmpty(); }

QString bindAddress() { return QStringLiteral("127.0.0.1"); }

// ── Pure helpers ────────────────────────────────────────────────────────────

Request parseRequest(const QByteArray &raw) {
  Request r;
  const int headerEnd = raw.indexOf("\r\n\r\n");
  if (headerEnd < 0)
    return r; // headers not fully received yet
  r.headersComplete = true;

  const QByteArray head = raw.left(headerEnd);
  const QList<QByteArray> lines = head.split('\n');
  if (lines.isEmpty())
    return r;

  // Request line: METHOD SP PATH SP VERSION
  const QList<QByteArray> reqLine =
      lines.first().trimmed().split(' ');
  if (reqLine.size() >= 2) {
    r.method = QString::fromLatin1(reqLine.at(0)).toUpper();
    r.path = QString::fromLatin1(reqLine.at(1));
  }
  for (int i = 1; i < lines.size(); ++i) {
    const QByteArray line = lines.at(i).trimmed();
    const int colon = line.indexOf(':');
    if (colon <= 0)
      continue;
    const QString key =
        QString::fromLatin1(line.left(colon)).trimmed().toLower();
    const QString val =
        QString::fromUtf8(line.mid(colon + 1)).trimmed();
    r.headers.insert(key, val);
  }

  r.body = raw.mid(headerEnd + 4);
  bool ok = false;
  const int declared =
      r.headers.value(QStringLiteral("content-length")).toInt(&ok);
  if (ok && declared >= 0)
    r.bodyComplete = r.body.size() >= declared;
  else
    r.bodyComplete = true; // no body expected
  return r;
}

bool authorized(const Request &req, const QString &token) {
  if (token.isEmpty())
    return false;
  const QString auth = req.headers.value(QStringLiteral("authorization"));
  const QString expected = QStringLiteral("Bearer ") + token;
  // Length-independent compare is overkill for a loopback token, but exact
  // string comparison here is fine and clear.
  return auth == expected;
}

bool parseSendBody(const QByteArray &body, Messaging::SendCommand *out,
                   QString *error) {
  const auto fail = [error](const QString &m) {
    if (error)
      *error = m;
    return false;
  };
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject())
    return fail(QStringLiteral("body is not a JSON object"));
  const QJsonObject o = doc.object();

  const QString to = o.value(QStringLiteral("to")).toString().trimmed();
  if (to.isEmpty())
    return fail(QStringLiteral("missing required field \"to\""));

  Messaging::Backend backend = Messaging::Backend::Web;
  if (o.contains(QStringLiteral("backend"))) {
    bool ok = false;
    backend = Messaging::parseBackend(
        o.value(QStringLiteral("backend")).toString(), &ok);
    if (!ok)
      return fail(QStringLiteral("unknown backend (use \"web\" or \"cloud\")"));
  }

  Messaging::SendCommand cmd;
  cmd.backend = backend;
  cmd.to = to;
  cmd.message = o.value(QStringLiteral("message")).toString();
  cmd.file = o.value(QStringLiteral("file")).toString();
  if (out)
    *out = cmd;
  return true;
}

QByteArray buildResponse(int status, const QByteArray &body,
                         const QByteArray &contentType) {
  static const QMap<int, QByteArray> reason{
      {200, "OK"},         {202, "Accepted"},
      {400, "Bad Request"}, {401, "Unauthorized"},
      {403, "Forbidden"},  {404, "Not Found"},
      {405, "Method Not Allowed"}, {500, "Internal Server Error"}};
  QByteArray out;
  out += "HTTP/1.1 " + QByteArray::number(status) + ' ' +
         reason.value(status, "Status") + "\r\n";
  out += "Content-Type: " + contentType + "\r\n";
  out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  out += "Connection: close\r\n";
  out += "\r\n";
  out += body;
  return out;
}

QByteArray jsonField(const QString &key, const QString &value) {
  QJsonObject o;
  o.insert(key, value);
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace LocalApi

// ── Server ──────────────────────────────────────────────────────────────────

LocalApiServer::LocalApiServer(QObject *parent) : QObject(parent) {}

bool LocalApiServer::start(QString *error) {
  stop();
  // Start if the send API is configured OR webhooks are on: the two share the
  // loopback port but are gated independently (bearer token vs verify token).
  if (!LocalApi::isConfigured() && !CloudWebhook::isEnabled()) {
    if (error)
      *error = QStringLiteral(
          "the local API is off (no token) and webhooks are disabled");
    return false;
  }
  m_server = new QTcpServer(this);
  connect(m_server, &QTcpServer::newConnection, this,
          &LocalApiServer::onNewConnection);
  if (!m_server->listen(QHostAddress(LocalApi::bindAddress()),
                        static_cast<quint16>(LocalApi::port()))) {
    if (error)
      *error = m_server->errorString();
    m_server->deleteLater();
    m_server = nullptr;
    return false;
  }
  return true;
}

void LocalApiServer::stop() {
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
}

bool LocalApiServer::isListening() const {
  return m_server && m_server->isListening();
}

int LocalApiServer::listeningPort() const {
  return m_server ? m_server->serverPort() : 0;
}

void LocalApiServer::onNewConnection() {
  while (m_server && m_server->hasPendingConnections()) {
    QTcpSocket *sock = m_server->nextPendingConnection();
    // Accumulate bytes until the whole request is in (requests are tiny and the
    // socket is loopback-only). The buffer is owned by the socket so it dies
    // with it.
    auto *buf = new QByteArray();
    connect(sock, &QObject::destroyed, this, [buf]() { delete buf; });

    connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf]() {
      buf->append(sock->readAll());
      const LocalApi::Request req = LocalApi::parseRequest(*buf);
      if (!req.headersComplete || !req.bodyComplete)
        return; // still waiting for the rest of the request
      serviceRequest(sock, req);
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

    // A client that connects but never sends a full request must not pin a
    // socket forever.
    QTimer::singleShot(5000, sock, [sock]() {
      if (sock->state() != QAbstractSocket::UnconnectedState)
        sock->disconnectFromHost();
    });
  }
}

void LocalApiServer::serviceRequest(QTcpSocket *sock,
                                    const LocalApi::Request &req) {
  using namespace LocalApi;
  const auto reply = [sock](int status, const QByteArray &json) {
    sock->write(buildResponse(status, json));
    sock->flush();
    sock->disconnectFromHost();
  };
  const auto replyText = [sock](int status, const QByteArray &text) {
    sock->write(buildResponse(status, text, "text/plain"));
    sock->flush();
    sock->disconnectFromHost();
  };

  // The Cloud API webhook endpoint. Not bearer-protected: Meta authenticates
  // via the GET verify-token handshake and the POST HMAC signature instead.
  if (req.path == QStringLiteral("/webhook") ||
      req.path.startsWith(QStringLiteral("/webhook?"))) {
    if (!CloudWebhook::isEnabled())
      return reply(404, jsonField(QStringLiteral("error"),
                                  QStringLiteral("webhook disabled")));
    if (req.method == QStringLiteral("GET")) {
      const QString challenge = CloudWebhook::verifyChallenge(
          CloudWebhook::parseQuery(req.path), CloudWebhook::verifyToken());
      if (challenge.isEmpty())
        return replyText(403, "verification failed");
      return replyText(200, challenge.toUtf8());
    }
    if (req.method == QStringLiteral("POST")) {
      if (!CloudWebhook::verifySignature(
              req.body, req.headers.value(QStringLiteral("x-hub-signature-256")),
              CloudWebhook::appSecret()))
        return reply(401, jsonField(QStringLiteral("error"),
                                    QStringLiteral("bad signature")));
      const auto incoming = CloudWebhook::parseIncoming(req.body);
      for (const auto &in : incoming)
        if (!in.text.isEmpty() && !in.from.isEmpty())
          emit webhookMessageReceived(in.from, in.text);
      // Always 200 so Meta does not retry a delivered event.
      return reply(200, jsonField(QStringLiteral("status"),
                                  QStringLiteral("received")));
    }
    return reply(405, jsonField(QStringLiteral("error"),
                                QStringLiteral("use GET or POST")));
  }

  if (!authorized(req, token()))
    return reply(401, jsonField(QStringLiteral("error"),
                                QStringLiteral("missing or invalid bearer token")));
  if (req.path != QStringLiteral("/send"))
    return reply(404, jsonField(QStringLiteral("error"),
                                QStringLiteral("unknown endpoint")));
  if (req.method != QStringLiteral("POST"))
    return reply(405, jsonField(QStringLiteral("error"),
                                QStringLiteral("use POST")));

  Messaging::SendCommand cmd;
  QString error;
  if (!parseSendBody(req.body, &cmd, &error))
    return reply(400, jsonField(QStringLiteral("error"), error));

  // The send itself is fire-and-forget (like the CLI over IPC): the window
  // performs it and the caller gets a 202. Validation errors above are the
  // synchronous failures.
  emit sendRequested(cmd);
  reply(202, jsonField(QStringLiteral("status"), QStringLiteral("accepted")));
}
