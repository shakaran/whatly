#include "diagnostics.h"
#include "settingsmanager.h"

#include <QFile>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

namespace Diagnostics {
namespace {
const char kEnabledKey[] = "diagnostics/scrollProbe";
const char kScriptName[] = "whatly-diagnostics-scroll-probe";
} // namespace

bool scrollProbeEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEnabledKey), false)
      .toBool();
}

void setScrollProbeEnabled(bool on) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEnabledKey),
                                                  on);
}

QString scrollProbeScript() {
  QFile f(QStringLiteral(":/scripts/scroll-smoothness-probe.js"));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

QString readbackScript() {
  // The probe stores its data under this key; hand it back verbatim, or "" when
  // it has recorded nothing yet.
  return QStringLiteral(
      "(function(){try{return localStorage.getItem('__whatly_scroll_probe')"
      "||'';}catch(e){return '';}})()");
}

void install(QWebEngineProfile *profile) {
  if (!profile)
    return;
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!scrollProbeEnabled())
    return;
  const QString src = scrollProbeScript();
  if (src.isEmpty())
    return;

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(src);
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace Diagnostics
