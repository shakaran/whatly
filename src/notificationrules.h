#ifndef NOTIFICATIONRULES_H
#define NOTIFICATIONRULES_H

#include <QString>
#include <QStringList>

class QDateTime;
class QSettings;

// Per-account rules that decide whether an incoming notification should raise a
// desktop popup. Two independent controls:
//   * Do Not Disturb — a daily time window (which may wrap past midnight) during
//     which popups are suppressed. Unread badges still update; only the popup is
//     held back.
//   * Keyword highlights — if the notification's title or body contains one of
//     these words (case-insensitive), it always notifies, even during DND.
//
// shouldNotify() is a pure function of the stored rules and the given time/text,
// so it is unit-tested directly.
namespace NotificationRules {

QSettings &settings();

bool dndEnabled();
QString dndStart();      // "HH:mm"
QString dndEnd();        // "HH:mm"
QStringList keywords();  // highlight words (may be empty)

// Per-contact profiles (idea #10): a VIP contact always notifies even during Do
// Not Disturb; a muted contact never pops up (its badge still updates). Matched
// case-insensitively against the notification title (the sender name).
QStringList vipContacts();
QStringList mutedContacts();

void setDndEnabled(bool enabled);
void setDndStart(const QString &hhmm);
void setDndEnd(const QString &hhmm);
void setKeywords(const QStringList &words);
void setVipContacts(const QStringList &names);
void setMutedContacts(const QStringList &names);

// Inline reply (idea #2): show a reply text field in the desktop notification
// (where the backend supports it) so a message can be answered without opening
// the window. Default on; it is only used where the notification server
// advertises the "inline-reply" capability. Linux only in practice.
bool inlineReplyEnabled();
void setInlineReplyEnabled(bool enabled);

// True when any entry in `names` is contained (case-insensitive) in `title`.
bool matchesContact(const QStringList &names, const QString &title);

// True when a notification with this title/body should pop up at `now`.
bool shouldNotify(const QDateTime &now, const QString &title,
                  const QString &body);

// Whether the given text matches any highlight keyword.
bool matchesKeyword(const QString &title, const QString &body);

// Whether `now`'s time-of-day falls inside the DND window (handles wrap-around).
bool inDndWindow(const QDateTime &now);

} // namespace NotificationRules

#endif // NOTIFICATIONRULES_H
