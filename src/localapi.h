#ifndef LOCALAPI_H
#define LOCALAPI_H

#include "messaging.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

// A tiny local HTTP endpoint that lets other programs on the same machine send
// a message through the running Whatly instance — the scriptable counterpart of
// `whatly --send`, but callable over HTTP (e.g. from a cron job, a home-server
// automation or another language). It binds to the loopback interface only and
// every request must carry a bearer token the user sets, so it is not reachable
// from the network and off by default.
//
// The request parsing, authorisation and JSON→SendCommand mapping are pure
// functions (unit-tested); the socket glue lives in LocalApiServer.
namespace LocalApi {

// ── Per-account configuration ──────────────────────────────────────────────
bool isEnabled();
void setEnabled(bool on);
int port();                 // default kDefaultPort if unset
void setPort(int port);
QString token();            // the bearer token; empty means "not configured"
void setToken(const QString &token);
bool isConfigured();        // enabled AND a non-empty token is set

constexpr int kDefaultPort = 8590;
// The server only ever listens here: a local API must not be exposed to the
// network, and loopback keeps it that way regardless of the port.
QString bindAddress();      // "127.0.0.1"

// ── Pure request/response helpers (unit-tested) ─────────────────────────────
struct Request {
  QString method;                    // upper-case, e.g. "POST"
  QString path;                      // e.g. "/send"
  QMap<QString, QString> headers;    // header names lower-cased
  QByteArray body;
  bool headersComplete = false;      // the blank line after the headers was seen
  bool bodyComplete = false;         // body length matches Content-Length
};

// Parse an accumulated request buffer. Tolerant of partial input: the *Complete
// flags say whether more bytes are still needed. A request with no
// Content-Length is treated as bodyComplete once the headers are in.
Request parseRequest(const QByteArray &raw);

// The Authorization header must be exactly "Bearer <token>" (token compared in
// full). An empty token never authorises anything.
bool authorized(const Request &req, const QString &token);

// Map the JSON body of POST /send to a SendCommand. Recognised fields:
//   "to"       (required) recipient, exactly as `--to` accepts it
//   "message"  the text / caption
//   "file"     absolute path to an attachment (optional)
//   "backend"  "web" (default) or "cloud"
// Returns false and fills *error on malformed JSON, a missing "to", or an
// unknown backend.
bool parseSendBody(const QByteArray &body, Messaging::SendCommand *out,
                   QString *error);

// Build an HTTP/1.1 response with the given body, content type and
// Connection: close. Defaults to JSON.
QByteArray buildResponse(int status, const QByteArray &body,
                         const QByteArray &contentType = "application/json");
// A one-field JSON object, e.g. jsonField("error","bad token") -> {"error":"…"}.
QByteArray jsonField(const QString &key, const QString &value);

} // namespace LocalApi

// The socket server. Lives on the GUI thread; a valid POST /send emits
// sendRequested() (the window performs the send) and the caller gets a 202.
class LocalApiServer : public QObject {
  Q_OBJECT
public:
  explicit LocalApiServer(QObject *parent = nullptr);

  // Start listening per the stored settings. Returns false (and fills *error)
  // if the API is not configured or the port cannot be bound. Restarting an
  // already-listening server stops it first.
  bool start(QString *error = nullptr);
  void stop();
  bool isListening() const;
  int listeningPort() const;

signals:
  // A well-formed, authorised send request. The receiver performs the actual
  // send (web through the page, cloud through the Cloud API).
  void sendRequested(const Messaging::SendCommand &cmd);
  // An incoming text message delivered by a Cloud API webhook (POST /webhook).
  // `from` is the sender's number. The receiver evaluates the auto-reply rules
  // and may reply through the Cloud API.
  void webhookMessageReceived(const QString &from, const QString &text);

private:
  void onNewConnection();
  void serviceRequest(QTcpSocket *sock, const LocalApi::Request &req);

  QTcpServer *m_server = nullptr;
};

#endif // LOCALAPI_H
