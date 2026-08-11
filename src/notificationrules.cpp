#include "notificationrules.h"
#include "settingsmanager.h"

#include <QDateTime>
#include <QTime>

namespace {
QTime parse(const QString &hhmm, const QTime &fallback) {
  const QTime t = QTime::fromString(hhmm, QStringLiteral("HH:mm"));
  return t.isValid() ? t : fallback;
}
} // namespace

namespace NotificationRules {

QSettings &settings() { return SettingsManager::instance().settings(); }

bool dndEnabled() {
  return settings().value(QStringLiteral("notif/dndEnabled"), false).toBool();
}
QString dndStart() {
  return settings().value(QStringLiteral("notif/dndStart"),
                          QStringLiteral("22:00")).toString();
}
QString dndEnd() {
  return settings().value(QStringLiteral("notif/dndEnd"),
                          QStringLiteral("08:00")).toString();
}
QStringList keywords() {
  const QStringList raw =
      settings().value(QStringLiteral("notif/keywords")).toStringList();
  QStringList out;
  for (const QString &w : raw) {
    const QString t = w.trimmed();
    if (!t.isEmpty())
      out << t;
  }
  return out;
}

static QStringList cleanList(const QStringList &raw) {
  QStringList out;
  for (const QString &w : raw) {
    const QString t = w.trimmed();
    if (!t.isEmpty())
      out << t;
  }
  return out;
}
QStringList vipContacts() {
  return cleanList(
      settings().value(QStringLiteral("notif/vipContacts")).toStringList());
}
QStringList mutedContacts() {
  return cleanList(
      settings().value(QStringLiteral("notif/mutedContacts")).toStringList());
}

void setDndEnabled(bool e) { settings().setValue(QStringLiteral("notif/dndEnabled"), e); }
void setDndStart(const QString &s) { settings().setValue(QStringLiteral("notif/dndStart"), s); }
void setDndEnd(const QString &s) { settings().setValue(QStringLiteral("notif/dndEnd"), s); }
void setKeywords(const QStringList &w) {
  settings().setValue(QStringLiteral("notif/keywords"), w);
}
void setVipContacts(const QStringList &n) {
  settings().setValue(QStringLiteral("notif/vipContacts"), cleanList(n));
}
void setMutedContacts(const QStringList &n) {
  settings().setValue(QStringLiteral("notif/mutedContacts"), cleanList(n));
}

bool inlineReplyEnabled() {
  return settings().value(QStringLiteral("notif/inlineReply"), true).toBool();
}
void setInlineReplyEnabled(bool e) {
  settings().setValue(QStringLiteral("notif/inlineReply"), e);
}

bool matchesContact(const QStringList &names, const QString &title) {
  for (const QString &n : names) {
    const QString t = n.trimmed();
    if (!t.isEmpty() && title.contains(t, Qt::CaseInsensitive))
      return true;
  }
  return false;
}

bool matchesKeyword(const QString &title, const QString &body) {
  const QStringList words = keywords();
  if (words.isEmpty())
    return false;
  const QString hay = (title + QLatin1Char(' ') + body);
  for (const QString &w : words)
    if (hay.contains(w, Qt::CaseInsensitive))
      return true;
  return false;
}

bool inDndWindow(const QDateTime &now) {
  const QTime t = now.time();
  const QTime start = parse(dndStart(), QTime(22, 0));
  const QTime end = parse(dndEnd(), QTime(8, 0));
  if (start == end)
    return false;                 // zero-length window: never
  if (start < end)
    return t >= start && t < end; // same-day window
  // Wrap-around window (e.g. 22:00 → 08:00): inside if after start OR before end.
  return t >= start || t < end;
}

bool manualActive(bool indefinite, const QDateTime &until,
                  const QDateTime &now) {
  if (indefinite)
    return true;
  return until.isValid() && now < until;
}

bool manualDndIndefinite() {
  return settings()
      .value(QStringLiteral("notif/manualDndIndefinite"), false)
      .toBool();
}
QDateTime manualDndUntil() {
  return QDateTime::fromString(
      settings().value(QStringLiteral("notif/manualDndUntil")).toString(),
      Qt::ISODate);
}
void dndSnoozeUntil(const QDateTime &until) {
  settings().setValue(QStringLiteral("notif/manualDndIndefinite"), false);
  settings().setValue(QStringLiteral("notif/manualDndUntil"),
                      until.toString(Qt::ISODate));
}
void dndOnIndefinite() {
  settings().setValue(QStringLiteral("notif/manualDndIndefinite"), true);
  settings().remove(QStringLiteral("notif/manualDndUntil"));
}
void dndOff() {
  settings().setValue(QStringLiteral("notif/manualDndIndefinite"), false);
  settings().remove(QStringLiteral("notif/manualDndUntil"));
}
bool manualDndActive(const QDateTime &now) {
  return manualActive(manualDndIndefinite(), manualDndUntil(), now);
}

bool shouldNotify(const QDateTime &now, const QString &title,
                  const QString &body) {
  // A muted contact is always silenced (badge still updates elsewhere); it wins
  // over everything, including a keyword hit.
  if (matchesContact(mutedContacts(), title))
    return false;
  // A VIP contact always breaks through, even during Do Not Disturb.
  if (matchesContact(vipContacts(), title))
    return true;
  // A keyword hit always breaks through, even during Do Not Disturb.
  if (matchesKeyword(title, body))
    return true;
  // Manual DND (on demand) and the daily schedule both suppress the popup.
  if (manualDndActive(now))
    return false;
  if (dndEnabled() && inDndWindow(now))
    return false;
  return true;
}

} // namespace NotificationRules
