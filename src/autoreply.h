#ifndef AUTOREPLY_H
#define AUTOREPLY_H

#include <QList>
#include <QString>

// Auto-reply ("listener") rules: react to an incoming WhatsApp message by
// sending back a defined response. This is the GUI-free rules engine — matching
// an incoming text against a set of rules and producing the reply text. The
// page-side observer that detects incoming messages, and the wiring that sends
// the reply, build on top; the store (settings + a rules file) does too. Keeping
// the matching pure makes it unit-testable and keeps regex/user input off the
// UI thread's critical path.
namespace AutoReply {

enum class MatchType {
  Exact,    // the whole message equals the pattern
  Contains, // the message contains the pattern
  Regex,    // the pattern is a regular expression; captures fill $1..$9 in reply
  Hashtag,  // the message contains the hashtag #pattern as a word
};

struct Rule {
  MatchType type = MatchType::Contains;
  QString pattern;
  QString reply;              // literal reply; for Regex, $1..$9 are captures
  bool caseSensitive = false;
  bool enabled = true;
};

// Whether `rule` matches `incoming`. For Regex, `captures` (if given) is filled
// with the whole match at [0] and capture groups at [1..]. Pure.
bool matches(const Rule &rule, const QString &incoming,
             QStringList *captures = nullptr);

// The reply for the first enabled rule that matches `incoming`, or an empty
// string if none match. For a Regex rule, $1..$9 (and $0 for the whole match)
// in the reply are replaced with the corresponding captures. Pure.
QString evaluate(const QString &incoming, const QList<Rule> &rules);

// Map a match-type name ("exact"/"contains"/"regex"/"hashtag", case-insensitive)
// to the enum; sets *ok to false for an unknown name (returns Contains).
MatchType parseMatchType(const QString &name, bool *ok = nullptr);
QString matchTypeName(MatchType type);

// ── Serialization ──────────────────────────────────────────────────────────
// Rules round-trip through JSON: [{ "match": "...", "pattern": "...",
// "reply": "...", "caseSensitive": false, "enabled": true }, ...]. This is what
// a user-edited rules file holds and what the per-account store keeps. Pure.
QByteArray rulesToJson(const QList<Rule> &rules);
QList<Rule> rulesFromJson(const QByteArray &json, QString *error = nullptr);

// ── Store (per account) ────────────────────────────────────────────────────
// The master switch: when off, no incoming message is ever auto-replied to.
bool isEnabled();
void setEnabled(bool on);

// Rules kept in the account's settings (managed from the CLI/UI).
QList<Rule> storedRules();
void setStoredRules(const QList<Rule> &rules);

// Optional path to a user-maintained JSON rules file (empty = none).
QString rulesFilePath();
void setRulesFilePath(const QString &path);

// The rules actually in effect: the stored rules followed by the file's rules
// (the file is re-read each call so edits take effect without a restart). A
// malformed file contributes nothing rather than throwing.
QList<Rule> activeRules();

// Given an incoming message, the reply to send, or empty if auto-reply is off
// or nothing matches. Convenience over isEnabled()/activeRules()/evaluate().
QString replyFor(const QString &incoming);

} // namespace AutoReply

#endif // AUTOREPLY_H
