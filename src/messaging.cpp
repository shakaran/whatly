#include "messaging.h"

#include <QRegularExpression>

namespace Messaging {

namespace {
// The characters that may appear in a phone number as typed: digits and the
// usual separators. Used to decide "this is a number, not a name".
bool looksLikePhone(const QString &s) {
  if (s.isEmpty())
    return false;
  int digits = 0;
  for (const QChar c : s) {
    if (c.isDigit())
      ++digits;
    else if (c != QLatin1Char('+') && c != QLatin1Char(' ') &&
             c != QLatin1Char('-') && c != QLatin1Char('(') &&
             c != QLatin1Char(')') && c != QLatin1Char('.'))
      return false; // a non-phone character => treat as a name
  }
  return digits >= 5; // too few digits to be a real number
}

QString digitsOnly(const QString &s) {
  QString out;
  for (const QChar c : s)
    if (c.isDigit())
      out.append(c);
  return out;
}
} // namespace

Recipient parseRecipient(const QString &to) {
  Recipient r;
  r.raw = to;
  const QString t = to.trimmed();
  if (t.isEmpty())
    return r; // Invalid

  if (t.startsWith(QLatin1String("group:"), Qt::CaseInsensitive)) {
    r.kind = RecipientKind::GroupId;
    r.value = digitsOnly(t.mid(6));
    return r;
  }
  if (t.endsWith(QLatin1String("@g.us"), Qt::CaseInsensitive)) {
    r.kind = RecipientKind::GroupId;
    r.value = digitsOnly(t.left(t.size() - 5));
    return r;
  }
  if (t.startsWith(QLatin1String("name:"), Qt::CaseInsensitive)) {
    r.kind = RecipientKind::ContactName;
    r.value = t.mid(5).trimmed();
    return r;
  }
  if (looksLikePhone(t)) {
    r.kind = RecipientKind::PhoneNumber;
    r.value = digitsOnly(t);
    return r;
  }
  r.kind = RecipientKind::ContactName;
  r.value = t;
  return r;
}

QStringList templatePlaceholders(const QString &body) {
  static const QRegularExpression re(QStringLiteral("\\{\\{\\s*([^{}]+?)\\s*\\}\\}"));
  QStringList names;
  auto it = re.globalMatch(body);
  while (it.hasNext()) {
    const QString name = it.next().captured(1);
    if (!names.contains(name))
      names.append(name);
  }
  return names;
}

QString fillTemplate(const QString &body, const QMap<QString, QString> &vars) {
  static const QRegularExpression re(QStringLiteral("\\{\\{\\s*([^{}]+?)\\s*\\}\\}"));
  QString out;
  out.reserve(body.size());
  qsizetype last = 0;
  auto it = re.globalMatch(body);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString key = m.captured(1);
    if (!vars.contains(key))
      continue; // leave an unknown placeholder untouched
    out.append(body.mid(last, m.capturedStart() - last));
    out.append(vars.value(key));
    last = m.capturedEnd();
  }
  out.append(body.mid(last));
  return out;
}

QMap<QString, QString> parseVars(const QStringList &kvs) {
  QMap<QString, QString> out;
  for (const QString &kv : kvs) {
    const qsizetype eq = kv.indexOf(QLatin1Char('='));
    if (eq <= 0)
      continue; // no '=', or an empty key
    out.insert(kv.left(eq), kv.mid(eq + 1));
  }
  return out;
}

Backend parseBackend(const QString &name, bool *ok) {
  const QString n = name.trimmed().toLower();
  if (ok)
    *ok = true;
  if (n == QLatin1String("web"))
    return Backend::Web;
  if (n == QLatin1String("cloud"))
    return Backend::Cloud;
  if (ok)
    *ok = false;
  return Backend::Web;
}

QString backendName(Backend backend) {
  return backend == Backend::Cloud ? QStringLiteral("cloud")
                                   : QStringLiteral("web");
}

} // namespace Messaging
