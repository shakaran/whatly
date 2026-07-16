#include "customcss.h"
#include "settingsmanager.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatsie-custom-css";
static const char kStyleId[] = "whatsie-custom-css";
static const char kSettingsKey[] = "customCssEnabled";

// __CSS__ becomes a JS string literal: the user's stylesheet, or "" to remove
// it. Re-running this on a loaded page swaps the CSS without a reload.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  try {
    var CSS = __CSS__;
    var el = document.getElementById('__STYLE_ID__');
    if (!CSS) { if (el) el.remove(); return; }
    if (!el) {
      el = document.createElement('style');
      el.id = '__STYLE_ID__';
      (document.head || document.documentElement).appendChild(el);
    }
    el.textContent = CSS;
  } catch (e) { /* never break the page */ }
})();
)JS";

namespace {

QString cssPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/custom.css");
}

// A JS double-quoted string literal from arbitrary CSS text.
QString jsStringLiteral(const QString &value) {
  QString escaped = value;
  escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
  escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
  escaped.replace(QLatin1Char('\r'), QString());
  return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

namespace CustomCss {

bool isActive() {
  return SettingsManager::instance()
             .settings()
             .value(QLatin1String(kSettingsKey), false)
             .toBool() &&
         !css().isEmpty();
}

QString css() {
  QFile file(cssPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(file.readAll());
}

bool setFromFile(const QString &path, QString *error) {
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error)
      *error = in.errorString();
    return false;
  }
  const QByteArray data = in.readAll();
  in.close();

  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dir)) {
    if (error)
      *error = QObject::tr("Cannot create %1").arg(dir);
    return false;
  }
  QFile out(cssPath());
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = out.errorString();
    return false;
  }
  out.write(data);
  out.close();

  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  true);
  return true;
}

void clear() {
  QFile::remove(cssPath());
  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  false);
}

QString scriptSource() {
  QString source = QString::fromLatin1(kScriptTemplate);
  source.replace(QLatin1String("__CSS__"),
                 jsStringLiteral(isActive() ? css() : QString()));
  source.replace(QLatin1String("__STYLE_ID__"), QLatin1String(kStyleId));
  return source;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!isActive())
    return;

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace CustomCss
