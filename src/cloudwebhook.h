#ifndef CLOUDWEBHOOK_H
#define CLOUDWEBHOOK_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

// Meta WhatsApp Business Cloud API webhooks: the receiving side of the Cloud
// backend. Meta calls a public URL with a GET verification handshake once, then
// POSTs incoming-message events. Whatly serves these on the same loopback port
// as the local API (LocalApiServer), so a tunnel (cloudflared/ngrok/reverse
// proxy) forwarding to 127.0.0.1 makes the endpoint reachable without ever
// binding Whatly itself to the network.
//
// The verification, signature check and payload parsing are pure functions
// (unit-tested); LocalApiServer routes /webhook to them and hands each incoming
// message to the auto-reply engine, which can reply through the Cloud API.
namespace CloudWebhook {

// ── Per-account configuration ──────────────────────────────────────────────
bool isEnabled();
void setEnabled(bool on);
QString verifyToken();          // the token echoed in the GET handshake
void setVerifyToken(const QString &token);
QString appSecret();            // Meta app secret, for POST signature checks
void setAppSecret(const QString &secret);

// ── Pure helpers (unit-tested) ──────────────────────────────────────────────
// Split the query string of a request path ("/webhook?a=b&c=d") into a map,
// percent-decoding keys and values. A path with no '?' yields an empty map.
QMap<QString, QString> parseQuery(const QString &path);

// The GET verification handshake: if hub.mode is "subscribe" and
// hub.verify_token equals `expectedToken` (non-empty), return hub.challenge so
// the caller can echo it back; otherwise return an empty string (reject).
QString verifyChallenge(const QMap<QString, QString> &query,
                        const QString &expectedToken);

// Validate the X-Hub-Signature-256 header ("sha256=<hex>") against the raw body
// using HMAC-SHA256 keyed with `appSecret`. An empty appSecret disables the
// check (returns true) so the feature works before a secret is set; an empty or
// malformed header with a non-empty secret fails.
bool verifySignature(const QByteArray &body, const QString &signatureHeader,
                     const QString &appSecret);

struct Incoming {
  QString from; // sender's phone number (digits, as Meta sends it)
  QString text; // the message body (text messages only for now)
  QString type; // "text", "image", … (from the payload)
  QString id;   // the wamid
};

// Extract incoming *text* messages from a webhook POST body. Non-text messages
// are returned with their type set but an empty text. Malformed JSON yields an
// empty list. Pure.
QList<Incoming> parseIncoming(const QByteArray &body);

} // namespace CloudWebhook

#endif // CLOUDWEBHOOK_H
