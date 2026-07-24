#ifndef MESSAGING_H
#define MESSAGING_H

#include <QMap>
#include <QString>
#include <QStringList>

// Core, GUI-free helpers for the "send a message by command / API" feature: it
// parses a recipient given on the command line, fills Whatly's own reusable
// message templates, and maps the backend name. Everything here is a pure
// function of its inputs, so it is unit-tested directly; the transport (the Web
// session automation and the Meta Cloud API) lives elsewhere and builds on top.
namespace Messaging {

// Which transport sends the message.
enum class Backend {
  Web,   // through the running app's WhatsApp Web session (unofficial)
  Cloud, // Meta WhatsApp Business Cloud API (official)
};

enum class RecipientKind {
  PhoneNumber, // an individual, addressed by number (digits, international)
  GroupId,     // a group, addressed by its WhatsApp id
  ContactName, // a saved contact/chat, addressed by display name
  Invalid,     // could not be parsed (e.g. empty)
};

struct Recipient {
  RecipientKind kind = RecipientKind::Invalid;
  QString value; // normalised: digits for a number, bare id for a group, the
                 // trimmed name for a contact
  QString raw;   // exactly what the user passed, for messages/logging
};

// Parse a --to value. Rules, in order:
//   "group:<id>" or "<id>@g.us"      -> GroupId  (value = the bare id digits)
//   "name:<text>"                    -> ContactName (forces a name lookup)
//   leading '+' or only phone chars  -> PhoneNumber (value = digits only)
//   anything else                    -> ContactName (value = trimmed text)
// Empty/whitespace yields RecipientKind::Invalid.
Recipient parseRecipient(const QString &to);

// The name of every {{placeholder}} in `body`, in first-seen order, without
// duplicates. Surrounding whitespace inside the braces is ignored, so
// "{{ name }}" and "{{name}}" are the same placeholder "name".
QStringList templatePlaceholders(const QString &body);

// Replace each {{key}} in `body` with vars[key]. Placeholders with no matching
// key are left untouched (so a missing value is visible rather than silently
// blank); callers validate with templatePlaceholders() + the keys they have.
QString fillTemplate(const QString &body, const QMap<QString, QString> &vars);

// Parse "key=value" strings (e.g. repeated --var) into a map. The first '='
// splits; a string with no '=' is ignored. Later duplicates win.
QMap<QString, QString> parseVars(const QStringList &kvs);

// Map a backend name ("web"/"cloud", case-insensitive) to the enum. Sets *ok to
// false for an unknown name (and returns Backend::Web).
Backend parseBackend(const QString &name, bool *ok = nullptr);
QString backendName(Backend backend);

// A send request passed from a `whatly --send …` invocation to the already
// running instance of the same profile (over SingleApplication's IPC).
struct SendCommand {
  Backend backend = Backend::Web;
  QString to;      // raw recipient, exactly as given on the command line
  QString message; // the text to send (the caption, when a file is attached)
  QString file;    // absolute path to an attachment, or empty for a text-only
                   // message; resolved by the CLI before it is sent over IPC
};

// Encode/decode a SendCommand as a single IPC payload. A tagged, JSON-bodied
// line is used rather than the space-joined argv the other CLI commands share,
// because a message contains spaces (and newlines and unicode) that argv
// splitting would mangle. decodeSendCommand returns false (and leaves *out
// untouched) when the payload is not a send command, so the receiver can fall
// through to its other IPC handling.
QString encodeSendCommand(const SendCommand &cmd);
bool decodeSendCommand(const QString &payload, SendCommand *out);

} // namespace Messaging

#endif // MESSAGING_H
