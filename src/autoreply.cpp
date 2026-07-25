#include "autoreply.h"
#include "settingsmanager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

namespace AutoReply {

bool matches(const Rule &rule, const QString &incoming, QStringList *captures) {
  if (rule.pattern.isEmpty())
    return false;
  const Qt::CaseSensitivity cs =
      rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

  switch (rule.type) {
  case MatchType::Exact:
    return incoming.trimmed().compare(rule.pattern.trimmed(), cs) == 0;

  case MatchType::Contains:
    return incoming.contains(rule.pattern, cs);

  case MatchType::Hashtag: {
    // Match "#pattern" as a whole token, e.g. rule pattern "sale" matches
    // "big #sale today". The '#' is implied, so a leading one is tolerated.
    QString tag = rule.pattern.trimmed();
    while (tag.startsWith(QLatin1Char('#')))
      tag = tag.mid(1);
    if (tag.isEmpty())
      return false;
    QRegularExpression re(QStringLiteral("(?:^|\\s)#") +
                          QRegularExpression::escape(tag) +
                          QStringLiteral("(?=\\s|$|[^\\w])"));
    if (!rule.caseSensitive)
      re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    return re.match(incoming).hasMatch();
  }

  case MatchType::Regex: {
    QRegularExpression re(rule.pattern);
    if (!rule.caseSensitive)
      re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
      return false;
    const QRegularExpressionMatch m = re.match(incoming);
    if (!m.hasMatch())
      return false;
    if (captures)
      *captures = m.capturedTexts();
    return true;
  }
  }
  return false;
}

QString evaluate(const QString &incoming, const QList<Rule> &rules) {
  for (const Rule &rule : rules) {
    if (!rule.enabled)
      continue;
    QStringList caps;
    if (!matches(rule, incoming, &caps))
      continue;
    QString reply = rule.reply;
    // For a regex rule, substitute $0 (whole match) and $1..$9 (groups).
    if (rule.type == MatchType::Regex) {
      for (int i = qMin(caps.size() - 1, 9); i >= 0; --i)
        reply.replace(QStringLiteral("$%1").arg(i), caps.at(i));
    }
    return reply;
  }
  return QString();
}

MatchType parseMatchType(const QString &name, bool *ok) {
  const QString n = name.trimmed().toLower();
  if (ok)
    *ok = true;
  if (n == QLatin1String("exact"))
    return MatchType::Exact;
  if (n == QLatin1String("contains"))
    return MatchType::Contains;
  if (n == QLatin1String("regex"))
    return MatchType::Regex;
  if (n == QLatin1String("hashtag"))
    return MatchType::Hashtag;
  if (ok)
    *ok = false;
  return MatchType::Contains;
}

QString matchTypeName(MatchType type) {
  switch (type) {
  case MatchType::Exact:
    return QStringLiteral("exact");
  case MatchType::Contains:
    return QStringLiteral("contains");
  case MatchType::Regex:
    return QStringLiteral("regex");
  case MatchType::Hashtag:
    return QStringLiteral("hashtag");
  }
  return QStringLiteral("contains");
}

QByteArray rulesToJson(const QList<Rule> &rules) {
  QJsonArray arr;
  for (const Rule &r : rules) {
    QJsonObject o;
    o["match"] = matchTypeName(r.type);
    o["pattern"] = r.pattern;
    o["reply"] = r.reply;
    o["caseSensitive"] = r.caseSensitive;
    o["enabled"] = r.enabled;
    arr.append(o);
  }
  return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QList<Rule> rulesFromJson(const QByteArray &json, QString *error) {
  QList<Rule> out;
  if (json.trimmed().isEmpty())
    return out;
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (error)
      *error = pe.errorString();
    return out;
  }
  if (!doc.isArray()) {
    if (error)
      *error = QStringLiteral("expected a JSON array of rules");
    return out;
  }
  const QJsonArray arr = doc.array();
  for (const QJsonValue &v : arr) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    Rule r;
    r.type = parseMatchType(o.value("match").toString());
    r.pattern = o.value("pattern").toString();
    r.reply = o.value("reply").toString();
    r.caseSensitive = o.value("caseSensitive").toBool(false);
    r.enabled = o.value("enabled").toBool(true);
    if (!r.pattern.isEmpty())
      out.append(r);
  }
  return out;
}

namespace {
QSettings &settings() { return SettingsManager::instance().settings(); }
} // namespace

bool isEnabled() {
  return settings().value(QStringLiteral("autoReply/enabled"), false).toBool();
}
void setEnabled(bool on) {
  settings().setValue(QStringLiteral("autoReply/enabled"), on);
}

QList<Rule> storedRules() {
  return rulesFromJson(
      settings().value(QStringLiteral("autoReply/rulesJson")).toByteArray());
}
void setStoredRules(const QList<Rule> &rules) {
  settings().setValue(QStringLiteral("autoReply/rulesJson"), rulesToJson(rules));
}

QString rulesFilePath() {
  return settings().value(QStringLiteral("autoReply/rulesFile")).toString();
}
void setRulesFilePath(const QString &path) {
  settings().setValue(QStringLiteral("autoReply/rulesFile"), path);
}

QList<Rule> activeRules() {
  QList<Rule> all = storedRules();
  const QString path = rulesFilePath();
  if (!path.isEmpty()) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
      all += rulesFromJson(f.readAll());
  }
  return all;
}

QString replyFor(const QString &incoming) {
  if (!isEnabled())
    return QString();
  return evaluate(incoming, activeRules());
}

} // namespace AutoReply
