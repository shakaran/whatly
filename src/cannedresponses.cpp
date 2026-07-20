#include "cannedresponses.h"
#include "settingsmanager.h"

#include <QSettings>

namespace {
const char kTitlesKey[] = "canned/titles";
const char kTextsKey[] = "canned/texts";

// A JS double-quoted string literal from arbitrary text.
QString jsString(const QString &value) {
  QString e = value;
  e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  e.replace(QLatin1Char('"'), QLatin1String("\\\""));
  e.replace(QLatin1Char('\n'), QLatin1String("\\n"));
  e.replace(QLatin1Char('\r'), QString());
  return QLatin1Char('"') + e + QLatin1Char('"');
}
} // namespace

namespace CannedResponses {

QList<Response> all() {
  QSettings &s = SettingsManager::instance().settings();
  const QStringList titles = s.value(QLatin1String(kTitlesKey)).toStringList();
  const QStringList texts = s.value(QLatin1String(kTextsKey)).toStringList();
  QList<Response> out;
  for (int i = 0; i < titles.size() && i < texts.size(); ++i)
    if (!titles.at(i).trimmed().isEmpty())
      out.append({titles.at(i), texts.at(i)});
  return out;
}

void setAll(const QList<Response> &responses) {
  QStringList titles, texts;
  for (const Response &r : responses) {
    titles << r.title;
    texts << r.text;
  }
  QSettings &s = SettingsManager::instance().settings();
  s.setValue(QLatin1String(kTitlesKey), titles);
  s.setValue(QLatin1String(kTextsKey), texts);
}

void add(const QString &title, const QString &text) {
  QList<Response> list = all();
  list.append({title.trimmed(), text});
  setAll(list);
}

void removeAt(int index) {
  QList<Response> list = all();
  if (index < 0 || index >= list.size())
    return;
  list.removeAt(index);
  setAll(list);
}

QString insertScript(const QString &text) {
  QString js = QString::fromLatin1(R"JS(
(function () {
  'use strict';
  try {
    var TEXT = __TEXT__;
    var box =
      document.querySelector('footer [contenteditable="true"]') ||
      document.querySelector('div[data-tab="10"][contenteditable="true"]');
    if (!box) return;
    box.focus();
    // execCommand keeps WhatsApp's own input handling in the loop, so the
    // Send button enables and drafts are tracked as if it were typed.
    document.execCommand('insertText', false, TEXT);
  } catch (e) { /* never break the page */ }
})();
)JS");
  js.replace(QLatin1String("__TEXT__"), jsString(text));
  return js;
}

} // namespace CannedResponses
