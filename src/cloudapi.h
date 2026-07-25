#ifndef CLOUDAPI_H
#define CLOUDAPI_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// Meta WhatsApp Business Cloud API sender (the `--backend cloud` path). Unlike
// the web backend this needs no running GUI/session: it POSTs directly to
// graph.facebook.com using a phone-number id and an access token the user
// provides. The URL/payload builders are pure (unit-tested); the blocking send
// lives here too so the CLI can do a cloud send and exit.
//
// The access token is a credential the *user* supplies (via Settings or a CLI
// setter) and is stored in the account's own config; Whatly never obtains it
// itself.
namespace CloudApi {

// ── Per-account configuration ──────────────────────────────────────────────
QString phoneNumberId();
void setPhoneNumberId(const QString &id);
QString accessToken();
void setAccessToken(const QString &token);
QString apiVersion();            // e.g. "v21.0"; a sensible default if unset
void setApiVersion(const QString &version);
bool isConfigured();             // phone-number id and token both present

// ── Pure request builders (unit-tested) ────────────────────────────────────
QString messagesUrl(const QString &apiVersion, const QString &phoneNumberId);
QString mediaUrl(const QString &apiVersion, const QString &phoneNumberId);

// `to` is normalised to digits (E.164 without '+') inside these.
QByteArray textPayload(const QString &to, const QString &body);
QByteArray mediaPayload(const QString &to, const QString &type,
                        const QString &mediaId, const QString &caption);
// Template message: positional {{1}},{{2}}… body parameters.
QByteArray templatePayload(const QString &to, const QString &templateName,
                           const QString &langCode, const QStringList &bodyParams);

// The Cloud API media "type" for a file, from its MIME ("image"/"video"/
// "audio"/"document"). Pure.
QString mediaTypeForMime(const QString &mime);

// ── Blocking send (used by the CLI) ────────────────────────────────────────
struct Result {
  bool ok = false;
  int httpStatus = 0;
  QString messageId;  // the wamid on success
  QString response;   // raw body (for diagnostics)
  QString error;      // human-readable error (network or API)
};

Result sendText(const QString &to, const QString &body);
// Uploads the local file, then sends it as media with an optional caption.
Result sendMediaFile(const QString &to, const QString &filePath,
                     const QString &caption);
Result sendTemplate(const QString &to, const QString &templateName,
                    const QString &langCode, const QStringList &bodyParams);

} // namespace CloudApi

#endif // CLOUDAPI_H
