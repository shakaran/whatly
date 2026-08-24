#include "hdmedia.h"
#include "settingsmanager.h"

#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-hd-media";
static const char kSettingsKey[] = "hdMediaDefault";

// __ON__ becomes true/false. A single long-lived MutationObserver watches for
// the media editor's quality control and asks for HD once per editor session.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  try {
    if (window.__whatlyHdObserver) {
      window.__whatlyHdObserver.disconnect();
      window.__whatlyHdObserver = null;
    }
    if (!__ON__) return;

    // The control is found by the name WhatsApp writes inside its own icon,
    // which is neither translated nor changed by the media. Its LABEL is both:
    // "Photo quality" when the attachment can go HD, and the reason it cannot
    // ("This media is not HD resolution.") when it cannot. Looking for the word
    // HD in that label therefore found this control only in the state where
    // clicking it can do nothing but open a dialog — so HD was never actually
    // enabled for media that could take it, and every attachment that could not
    // cost the user a dialog to dismiss. See issue #96.
    var ICON = 'wds-ic-hd-settings';

    function findHd() {
      var wrap = document.querySelector('[data-testid="' + ICON + '"]');
      if (wrap) {
        var b = wrap.querySelector('button, [role="button"]') ||
                wrap.closest('button, [role="button"]');
        if (b) return b;
      }
      var el = document.querySelector(
        '[data-icon="' + ICON + '"], [data-icon="hd"], [data-icon="media-hd"]');
      if (el) return el.closest('button, [role="button"]') || el;
      // Last resort: the name is also the title inside the icon's svg.
      var cands = document.querySelectorAll('button, [role="button"]');
      for (var i = 0; i < cands.length; i++)
        if ((cands[i].textContent || '').indexOf(ICON) !== -1) return cands[i];
      return null;
    }

    // Whether this attachment can go HD at all, asked before anything is
    // clicked. WhatsApp dims the icon when it cannot: the icon's wrapper gains a
    // class whose whole effect is opacity — 0.45 against 1 in the usable state —
    // while the control itself is identical in both, right down to
    // aria-disabled="false". The generated class name changes between WhatsApp
    // deployments, so what is read is the opacity it produces, not its name.
    function dimmed(btn) {
      // The control itself and every part of the icon, not just the first part:
      // which element carries the dimming is WhatsApp's business and could move,
      // and findHd() may return the button, the wrapper or the icon. In the
      // usable state all of them measure 1, so asking all of them cannot produce
      // a false positive; asking only some of them would let a dimmed control
      // look usable.
      var parts = [btn];
      var inside = btn.querySelectorAll('span, svg, path');
      for (var i = 0; i < inside.length; i++) parts.push(inside[i]);
      for (var j = 0; j < parts.length; j++) {
        var o = parseFloat(window.getComputedStyle(parts[j]).opacity);
        if (!isNaN(o) && o < 0.9) return true;
      }
      return false;
    }

    function labelOf(el) {
      return (el.getAttribute('aria-label') || el.getAttribute('title') ||
              '').trim();
    }

    // A seatbelt for the day WhatsApp stops dimming the icon: if a click is
    // answered by the refusal dialog after all, the wording is remembered and
    // not provoked again. It repeats the control's own label, so this recognises
    // the state in any language. With the check above doing its job nothing ever
    // reaches here.
    var refused = Object.create(null);

    function refusalFor(label) {
      if (!label) return false;
      var dialogs = document.querySelectorAll('[role="dialog"]');
      for (var i = 0; i < dialogs.length; i++)
        if ((dialogs[i].textContent || '').indexOf(label) !== -1) return true;
      return false;
    }

    // The quality choices open as a list beside the control; the HD one names
    // itself. Anything inside the conversation is skipped, so a message that
    // merely says "HD" cannot be mistaken for a menu entry.
    function chooseHd() {
      var items = document.querySelectorAll(
        '[role="menuitem"], [role="radio"], li[role="button"], li');
      for (var i = 0; i < items.length; i++) {
        var item = items[i];
        if (!item.offsetParent) continue;
        if (item.closest('#main')) continue;
        if (!/\bHD\b/.test(item.textContent || '')) continue;
        try { item.click(); } catch (e) {}
        return true;
      }
      return false;
    }

    // What became of the click: a refusal to remember, or a list to choose from.
    // The list can take a moment to appear, so look a few times before giving up.
    function settle(label, attempt) {
      try {
        if (refusalFor(label)) { refused[label] = true; acting = false; return; }
        if (chooseHd()) { acting = false; return; }
        if (attempt < 3) {
          setTimeout(function () { settle(label, attempt + 1); }, 250);
          return;
        }
      } catch (e) { /* fall through and release */ }
      acting = false;
    }

    // Act at most once per editor session. `tried` gates re-clicks; it resets
    // only when the editor (and its control) is gone, so the next attachment
    // gets one attempt.
    var acting = false, tried = false;
    function tryEnable() {
      if (acting) return;
      var btn = findHd();
      if (!btn) { tried = false; return; }   // editor closed: arm for next time
      if (tried) return;                      // already handled this editor
      var label = labelOf(btn);
      if (refused[label]) return;             // known impossible: leave it alone
      // Not marked as tried: a dim caught mid-fade corrects itself on the next
      // mutation, and a control that is genuinely dimmed is simply never asked.
      if (dimmed(btn)) return;
      tried = true;                           // mark before clicking, so a
                                              // rejection dialog cannot retrigger
      if (btn.getAttribute('aria-pressed') === 'true') return; // already on
      acting = true;
      try { btn.click(); } catch (e) {}
      setTimeout(function () { settle(label, 1); }, 200);
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
