#include "linkeddevicename.h"
#include "settingsmanager.h"

#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-linked-device-name";

// __NAME__ becomes a JS string literal ("Whatly for Linux", or "" when the
// setting is off). The label comes from WAWebBrowserInfo — a module whose
// default export is a 0-arg function returning {os, name, version, ua} — so
// the hook swaps the module registry's defaultExport for a decorating wrapper
// (older WhatsApp Web builds exposed the same function as
// WAWebMiscBrowserUtils.info; that path is kept as a fallback).
//
// The phone renders the label as "Browser (OS)", where the os field displays
// arbitrary text but the browser name is validated against known browsers
// and silently omitted otherwise (both behaviors observed by re-linking a
// phone). The wrapper exploits that: os carries the full brand text and name
// is set to the unrecognized "Whatly", so the browser prefix disappears and
// the label reads just "Whatly for Linux".
//
// The wrapper reads the LIVE name from window.__whatlyLinkedDeviceName, so
// re-running this script on a loaded page toggles the override without a
// reload. The module system loads asynchronously, so the hook retries until
// it resolves.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  var NAME = __NAME__;
  window.__whatlyLinkedDeviceName = NAME;  // live value read by the wrapper
  if (window.__whatlyLinkedDeviceNameHooked || !NAME) return;
  var wrap = function (orig) {
    return function () {
      var inf = orig.apply(this, arguments);
      try {
        if (window.__whatlyLinkedDeviceName && inf) {
          inf.os = window.__whatlyLinkedDeviceName;
          inf.name = 'Whatly';  // unknown browser → phone omits the prefix
        }
      } catch (e) { /* keep the stock label */ }
      return inf;
    };
  };
  // The phone draws the linked device as an icon plus the "os" text. The text
  // is arbitrary (that is how "Whatly for Linux" gets there), but the icon is
  // chosen from WhatsApp's own set by the *client type*, which
  // getCompanionWebClientFromBrowser() derives from the browser name. Our name
  // is deliberately unrecognized (to drop the "Google Chrome" prefix), so that
  // lookup falls to OTHER_WEB_CLIENT — which has no icon, leaving it blank.
  // Report the desktop-app client instead, so a computer icon shows next to the
  // still-prefix-free label. The desktop value is the one the function returns
  // on Windows; capture it at run time by flipping isWindows for that one call
  // rather than hardcoding an enum number that WhatsApp could renumber.
  var hookDeviceIcon = function (modules) {
    if (window.__whatlyDeviceIconHooked) return;
    if (!modules['WAWebCompanionRegClientUtils'] || !modules['WAWebEnvironment'])
      return;
    var cru = window.require('WAWebCompanionRegClientUtils');
    var env = window.require('WAWebEnvironment');
    if (!cru || typeof cru.getCompanionWebClientFromBrowser !== 'function' || !env)
      return;
    var saved = env.isWindows;
    var desktop;
    try {
      env.isWindows = true;
      desktop = cru.getCompanionWebClientFromBrowser();
    } finally {
      env.isWindows = saved;
    }
    cru.getCompanionWebClientFromBrowser = function () { return desktop; };
    window.__whatlyDeviceIconHooked = true;
  };

  var hookName = function (modules) {
    if (window.__whatlyLinkedDeviceNameHooked) return;
    // Current builds: bare-function module, patched via the registry.
    var rec = modules['WAWebBrowserInfo'];
    if (rec && typeof rec.defaultExport === 'function') {
      var wrapped = wrap(rec.defaultExport);
      rec.defaultExport = wrapped;
      if (rec.exports && typeof rec.exports.default === 'function')
        rec.exports.default = wrapped;
      window.__whatlyLinkedDeviceNameHooked = true;
      return;
    }
    // Older builds: plain export on WAWebMiscBrowserUtils. Ask the registry
    // first — require()ing a module WhatsApp does not have throws, and it
    // logs "Requiring unknown module" to the console before it does, once
    // per retry. Current builds no longer ship it at all.
    if (modules['WAWebMiscBrowserUtils']) {
      var mod = window.require('WAWebMiscBrowserUtils');
      if (mod && typeof mod.info === 'function') {
        mod.info = wrap(mod.info);
        window.__whatlyLinkedDeviceNameHooked = true;
      }
    }
  };

  var tries = 0;
  var hook = function () {
    try {
      if (typeof window.require === 'function') {
        var modules = window.require('__debug').modulesMap;
        // Each guarded by its own flag: the modules can register at different
        // times, so install whichever is ready and keep retrying for the rest.
        try { hookName(modules); } catch (e) { /* retry */ }
        try { hookDeviceIcon(modules); } catch (e) { /* retry */ }
      }
    } catch (e) { /* module system not registered yet — retry */ }
    if ((!window.__whatlyLinkedDeviceNameHooked ||
         !window.__whatlyDeviceIconHooked) && ++tries < 120)
      setTimeout(hook, 250);  // ~30s while the app boots
  };
  hook();
})();
)JS";

namespace LinkedDeviceName {

static QString platformDeviceName() {
#if defined(Q_OS_WIN)
  return QStringLiteral("Whatly for Windows");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("Whatly for macOS");
#else
  return QStringLiteral("Whatly for Linux");
#endif
}

static bool isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QStringLiteral("identifyInLinkedDevices"), true)
      .toBool();
}

QString scriptSource(const QString &accountLabel) {
  QString name = isEnabled() ? platformDeviceName() : QString();
  if (!name.isEmpty() && !accountLabel.isEmpty())
    name += QStringLiteral(" (%1)").arg(accountLabel);

  // The label ends up inside a JS double-quoted string literal, so a quote or
  // backslash in an account name must not break out of it.
  QString escaped = name;
  escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));

  QString source = QString::fromLatin1(kScriptTemplate);
  source.replace(QLatin1String("__NAME__"),
                 QStringLiteral("\"%1\"").arg(escaped));
  return source;
}

void install(QWebEngineProfile *profile, const QString &accountLabel) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!isEnabled())
    return; // disabled → do not inject on fresh loads

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource(accountLabel));
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace LinkedDeviceName
