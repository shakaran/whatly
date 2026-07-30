#include "hdmedia.h"
#include "settingsmanager.h"

#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-hd-media";
static const char kSettingsKey[] = "hdMediaDefault";

// __ON__ becomes true/false. A single long-lived MutationObserver watches for
// the media editor's HD control and enables HD once per editor session.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  try {
    if (window.__whatlyHdObserver) {
      window.__whatlyHdObserver.disconnect();
      window.__whatlyHdObserver = null;
    }
    if (!__ON__) return;

    // Find the HD control in the open media editor. WhatsApp marks it with an
    // "hd" data-icon (falling back to an aria-label / title containing "HD").
    function findHd() {
      var el = document.querySelector(
        '[data-icon="hd"], [data-icon="media-hd"]');
      if (el) return el.closest('button, [role="button"]') || el;
      var cands = document.querySelectorAll(
        'button[aria-label], [role="button"][aria-label], button[title]');
      for (var i = 0; i < cands.length; i++) {
        var label = (cands[i].getAttribute('aria-label') ||
                     cands[i].getAttribute('title') || '');
        if (/\bHD\b/.test(label)) return cands[i];
      }
      return null;
    }

    // Act at most once per editor session. Sub-HD media cannot be sent in HD:
    // WhatsApp answers a click with a "this media is not HD resolution" dialog
    // and leaves the control unpressed, so a retry loop re-opened that dialog
    // forever (issue #34). `tried` gates re-clicks; it resets only when the
    // editor (and its HD control) is gone, so the next attach gets one attempt.
    var acting = false, tried = false;
    function tryEnable() {
      if (acting) return;
      var btn = findHd();
      if (!btn) { tried = false; return; }   // editor closed: arm for next time
      if (tried) return;                      // already handled this editor
      tried = true;                           // mark before clicking, so a
                                              // rejection dialog cannot retrigger
      var pressed = btn.getAttribute('aria-pressed');
      if (pressed === 'true') return;         // HD already on: nothing to do
      acting = true;
      try { btn.click(); } catch (e) {}
      // WhatsApp sometimes opens a small menu; pick the HD-quality entry.
      setTimeout(function () {
        try {
          var items = document.querySelectorAll(
            '[role="menuitem"], [role="radio"], li');
          for (var i = 0; i < items.length; i++) {
            if (/\bHD\b/.test(items[i].textContent || '')) {
              items[i].click();
              break;
            }
          }
        } catch (e) {}
        acting = false;
      }, 120);
    }

    var obs = new MutationObserver(function () { tryEnable(); });
    obs.observe(document.body, { childList: true, subtree: true });
    window.__whatlyHdObserver = obs;
    tryEnable();
  } catch (e) { /* never break the page */ }
})();
)JS";

namespace HdMedia {

bool isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kSettingsKey), false)
      .toBool();
}

void setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  enabled);
}

QString scriptSource() {
  QString source = QString::fromLatin1(kScriptTemplate);
  source.replace(QLatin1String("__ON__"),
                 isEnabled() ? QLatin1String("true") : QLatin1String("false"));
  return source;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!isEnabled())
    return;

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace HdMedia
