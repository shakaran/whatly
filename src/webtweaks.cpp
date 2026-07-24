#include "webtweaks.h"
#include "settingsmanager.h"
#include <QObject>

#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-web-tweaks";
static const char kHdFlagScriptName[] = "whatly-hd-media-flag";

// WhatsApp Web ships its HD photo consumption UI dark, gated by the server-side
// rollout flag wa_web_show_hd_photo (off for most accounts). The HD originals
// arrive as hidden associated child messages that linked devices are not yet
// sent, so forcing the flag changes nothing visible today — HD badges render
// only where the data is actually present — but the moment the servers start
// delivering HD originals to linked/companion devices, HD media lights up
// without waiting for Meta's per-account rollout. This is the RECEIVE side;
// hdmedia.cpp separately handles sending in HD by default.
static const char kHdFlagScript[] = R"JS(
(function () {
  'use strict';
  if (window.__whatlyHdFlagReady) return;
  window.__whatlyHdFlagReady = true;
  var tries = 0;
  var timer = setInterval(function () {
    tries += 1;
    try {
      var ab = window.require('WAWebABProps');
      var orig = ab.getABPropConfigValue;
      ab.getABPropConfigValue = function (name) {
        if (name === 'wa_web_show_hd_photo') return true;
        return orig.apply(this, arguments);
      };
      clearInterval(timer);
    } catch (e) {
      if (tries > 240) clearInterval(timer);  // module system never appeared
    }
  }, 500);
})();
)JS";

// __FLAGS__ becomes a JSON object of the enabled tweaks. Behavior is gated by
// the LIVE flags object (window.__whatlyWebTweaks) rather than a captured
// boolean, so re-running this script on a loaded page toggles the tweak
// without a reload. Every DOM access is wrapped so a stale selector can never
// break the page.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  var FLAGS = __FLAGS__;
  var LABELS = __LABELS__;
  var W = window.__whatlyWebTweaks;
  if (W) {
    W.dismissExpressionsPanel = FLAGS.dismissExpressionsPanel;  // live update
    W.themeToggleButton = FLAGS.themeToggleButton;
    W.privacyBlurButton = FLAGS.privacyBlurButton;
  } else {
    W = window.__whatlyWebTweaks = FLAGS;
  }
  if (window.__whatlyWebTweaksReady) {
    // Re-run from Settings: the listeners are already in place, but the button
    // must be added or removed to match the flag that just changed.
    if (window.__whatlyInstallThemeButton) window.__whatlyInstallThemeButton();
    return;
  }
  window.__whatlyWebTweaksReady = true;

  // ── Dismiss the expressions (emoji/GIF/sticker) panel on outside click. ──
  // WhatsApp keeps it open until its button is pressed again. The panel mount
  // node '#expressions-panel-container' always exists (empty, height 0); it is
  // OPEN exactly when its child has a subtree. Close via a synthetic Escape.
  // OPEN means the panel's subtree is actually laid out on screen. A bare
  // child-count check is not enough: after a soft remount WhatsApp can leave a
  // stale, hidden subtree behind, so require a visibly sized box.
  var isOpen = function (c) {
    if (!(c && c.firstElementChild && c.firstElementChild.childElementCount > 0))
      return false;
    var els = c.querySelectorAll('*');
    for (var i = 0; i < els.length && i < 60; i++) {
      var r = els[i].getBoundingClientRect();
      if (r.width > 40 && r.height > 40) return true;
    }
    return false;
  };
  document.addEventListener('pointerdown', function (ev) {
    try {
      if (!W.dismissExpressionsPanel) return;
      var panels = document.querySelectorAll('#expressions-panel-container');
      var open = false, inside = false;
      for (var i = 0; i < panels.length; i++) {
        if (isOpen(panels[i])) open = true;
        if (panels[i].contains(ev.target)) inside = true;
      }
      if (!open || inside) return;
      // Never dismiss for a click anywhere in the emoji subsystem, at ANY
      // popover depth. The skin-tone / variant picker is a SEPARATE popover
      // mounted outside #expressions-panel-container whose swatches are
      // <img class="emojik"> with no data-emoji, so an emoji-cell-only check
      // missed them and closed the panel mid skin-tone selection. The "emojik"
      // sprite class is exclusive to the picker UI (chat and composer emojis
      // use "emoji" without the k), so matching it scopes cleanly.
      if (ev.target.closest && ev.target.closest(
          '[data-emoji], .emojik, .emoji-grid, [class*="emoji-variant"], ' +
          '[class*="skin-tone"], [class*="skintone"]')) return;
      var btn = ev.target.closest && ev.target.closest('button');
      if (btn && /emoji|gif|sticker/i.test(btn.getAttribute('aria-label') || ''))
        return;
      document.dispatchEvent(new KeyboardEvent('keydown', {
        key: 'Escape', code: 'Escape', keyCode: 27, which: 27, bubbles: true,
      }));
    } catch (e) { /* never break the page */ }
  }, true);

  // ── Our own entries in WhatsApp's nav rail, above the avatar. ─────────────
  // The rail's classes are obfuscated and change with every WhatsApp build, so
  // nothing here is matched by class: the avatar is found as the last button in
  // the rail that holds an <img>, and each of our buttons is a CLONE of a
  // neighbouring entry, which is what makes them look native without hardcoding
  // a single style.
  var ICON = {
    // Sun: shown while dark, i.e. click to go light.
    sun: '<circle cx="12" cy="12" r="4.2"/><g stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M12 2.6v2.2M12 19.2v2.2M2.6 12h2.2M19.2 12h2.2M5.3 5.3l1.6 1.6M17.1 17.1l1.6 1.6M18.7 5.3l-1.6 1.6M6.9 17.1l-1.6 1.6"/></g>',
    // Moon: shown while light, i.e. click to go dark.
    moon: '<path d="M20.3 14.6A8.6 8.6 0 0 1 9.4 3.7a8.6 8.6 0 1 0 10.9 10.9z"/>',
    // Open eye: shown while blurred, i.e. click to reveal.
    eye: '<path d="M12 5C6.9 5 2.7 9.3 1.5 12c1.2 2.7 5.4 7 10.5 7s9.3-4.3 10.5-7C21.3 9.3 17.1 5 12 5zm0 11.5a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9z"/><circle cx="12" cy="12" r="2.3"/>',
    // Struck-through eye: shown while clear, i.e. click to blur.
    eyeOff: '<path d="M12 5C6.9 5 2.7 9.3 1.5 12c.6 1.3 1.9 3 3.7 4.4l2-2A4.5 4.5 0 0 1 12 7.5c.5 0 1 .1 1.5.2l1.8-1.8A11 11 0 0 0 12 5zm7.3 1.3-1.6 1.6c1.4 1.1 2.5 2.5 3 3.1-1.1 2.4-4.7 6-9.7 6-.9 0-1.7-.1-2.5-.3l-1.8 1.8c1.3.4 2.8.7 4.3.7 5.1 0 9.3-4.3 10.5-7-.5-1.2-1.6-2.8-3.2-4.2z"/><path d="M4.3 20.4 3 19.1 19.1 3l1.3 1.3z"/>',
  };
  var isDark = function () {
    try {
      return document.documentElement.classList.contains('dark') ||
             document.body.classList.contains('dark') ||
             (localStorage.getItem('theme') || '').indexOf('dark') !== -1;
    } catch (e) { return false; }
  };
  // The blur is a stylesheet the app injects, so the page can read its own
  // state off it — no second channel to keep in sync with the app.
  var isBlurred = function () {
    var style = document.getElementById('whatly-privacy-blur');
    return !!(style && style.textContent);
  };

  // Every button we add: what it looks like right now, and what a click does.
  var BUTTONS = [
    {
      id: 'whatly-theme-toggle',
      enabled: function () { return W.themeToggleButton; },
      icon: function () { return isDark() ? ICON.sun : ICON.moon; },
      label: function () {
        return isDark() ? LABELS.switchToLight : LABELS.switchToDark;
      },
      click: function () {
        if (window.__whatlyBridge && window.__whatlyBridge.toggleTheme)
          window.__whatlyBridge.toggleTheme();
      },
    },
    {
      id: 'whatly-blur-toggle',
      enabled: function () { return W.privacyBlurButton; },
      icon: function () { return isBlurred() ? ICON.eye : ICON.eyeOff; },
      label: function () {
        return isBlurred() ? LABELS.showChats : LABELS.blurChats;
      },
      click: function () {
        if (window.__whatlyBridge && window.__whatlyBridge.togglePrivacyBlur)
          window.__whatlyBridge.togglePrivacyBlur();
      },
    },
  ];

  // Idempotent, and that is not an optimisation but a correctness requirement:
  // writing innerHTML is a DOM mutation, and this used to be called from a
  // MutationObserver watching the DOM. Every repaint retriggered the observer,
  // which repainted again — a feedback loop that burned ~40% of a core with the
  // app sitting idle. The label identifies the state, so if it has not changed
  // there is nothing to write.
  var paint = function (spec, button) {
    var label = spec.label();
    if (button.getAttribute('data-whatly-state') === label) return;
    var svg = button.querySelector('svg');
    if (!svg) return;
    button.setAttribute('data-whatly-state', label);
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('fill', 'currentColor');
    svg.innerHTML = spec.icon();
    button.setAttribute('aria-label', label);
    button.setAttribute('title', label);
  };
  // The buttons of the narrow left column, in document order.
  var railButtons = function () {
    return Array.prototype.slice.call(document.querySelectorAll('button'))
      .filter(function (b) {
        var r = b.getBoundingClientRect();
        return r.width > 0 && r.width <= 72 && r.left < 80;
      });
  };
  // Each rail entry is wrapped several levels deep (button > div > span > div),
  // so the avatar's own parent holds nothing but the avatar. Climb until the
  // level where entries are actually siblings of each other.
  var wrapperSharedWith = function (node, other) {
    while (node.parentElement) {
      if (node.parentElement.contains(other)) return node;
      node = node.parentElement;
    }
    return null;
  };
  // Is every button that should be there, there — and none that should not be?
  // Two getElementById calls. This runs on a timer, so it must not touch layout.
  var settled = function () {
    for (var n = 0; n < BUTTONS.length; n++) {
      var entry = document.getElementById(BUTTONS[n].id);
      if (BUTTONS[n].enabled() !== !!(entry && entry.isConnected)) return false;
    }
    return true;
  };

  var install = function () {
    if (settled()) return;   // the common case, and it costs nothing
    try {
      // Each one goes directly above the avatar, so inserting them in order
      // leaves them on screen in the order they are listed above.
      for (var n = 0; n < BUTTONS.length; n++) {
        var spec = BUTTONS[n];
        var existing = document.getElementById(spec.id);

        if (!spec.enabled()) {
          if (existing) existing.remove();
          continue;
        }
        if (existing && existing.isConnected) continue;

        var rail = railButtons();
        var avatar = null, template = null;
        for (var i = rail.length - 1; i >= 0; i--) {
          if (!avatar && rail[i].querySelector('img')) { avatar = rail[i]; continue; }
          if (avatar && !template && rail[i].querySelector('svg') &&
              !rail[i].querySelector('img') && !rail[i].closest('[id^="whatly-"]'))
            template = rail[i];
        }
        if (!avatar || !template) continue;

        var avatarWrapper = wrapperSharedWith(avatar, template);
        var templateWrapper = wrapperSharedWith(template, avatar);
        if (!avatarWrapper || !templateWrapper ||
            avatarWrapper.parentElement !== templateWrapper.parentElement) continue;

        // Clone a whole neighbouring entry rather than styling one from scratch:
        // every class in this rail is obfuscated and changes with each WhatsApp
        // build, and a clone is immune to that. cloneNode drops event listeners,
        // which is exactly what is wanted here.
        var entry = templateWrapper.cloneNode(true);
        entry.id = spec.id;
        var button = entry.querySelector('button') || entry;
        button.removeAttribute('data-navbar-item');
        // The entry it was copied from may have been the selected one.
        button.removeAttribute('aria-pressed');
        button.removeAttribute('aria-selected');
        button.removeAttribute('aria-current');
        paint(spec, button);
        entry.addEventListener('click', (function (s) {
          return function (ev) {
            ev.preventDefault();
            ev.stopPropagation();
            s.click();
          };
        })(spec), true);

        avatarWrapper.parentElement.insertBefore(entry, avatarWrapper);
      }
    } catch (e) { /* never break the page */ }
  };

  var repaint = function () {
    for (var n = 0; n < BUTTONS.length; n++) {
      var entry = document.getElementById(BUTTONS[n].id);
      if (entry) paint(BUTTONS[n], entry.querySelector('button') || entry);
    }
  };

  window.__whatlyInstallThemeButton = function () { install(); repaint(); };

  // WhatsApp rebuilds the rail on navigation, so the buttons have to be put
  // back. A MutationObserver over the body is the obvious way and the wrong
  // one: WhatsApp mutates its DOM continuously, so the callback ran constantly,
  // and each run measured the rail — a forced layout — several times a second.
  // A timer costs two getElementById calls per tick instead, and a button
  // reappearing up to a second after WhatsApp tore it out is not something
  // anyone can perceive.
  install();
  setInterval(install, 1000);

  // The icons follow state that changes without anything else on the page
  // moving: the theme lives in a class on <html>, the blur in a stylesheet in
  // <head>. Both observers are narrow, and paint() is a no-op when the state it
  // would draw is the state already drawn.
  new MutationObserver(repaint).observe(document.documentElement, {
    attributes: true, attributeFilter: ['class'],
  });
  new MutationObserver(repaint).observe(document.head, {
    childList: true, subtree: true, characterData: true,
  });
})();
)JS";

namespace WebTweaks {

QString hdFlagScriptSource() { return QString::fromLatin1(kHdFlagScript); }

static const char *jsBool(bool value) { return value ? "true" : "false"; }

// A translated string as a JS double-quoted literal — the injected button
// labels are user-facing, so they go through tr() here and are handed to the
// script rather than hardcoded in English inside the JS (where lupdate could
// never see them).
static QString jsString(const QString &value) {
  QString e = value;
  e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  e.replace(QLatin1Char('"'), QLatin1String("\\\""));
  return QLatin1Char('"') + e + QLatin1Char('"');
}

QString scriptSource() {
  QSettings &s = SettingsManager::instance().settings();
  const bool dismiss =
      s.value(QStringLiteral("webtweaks/dismissExpressionsPanel"), false).toBool();
  const bool themeButton =
      s.value(QStringLiteral("webtweaks/themeToggleButton"), true).toBool();
  const bool blurButton =
      s.value(QStringLiteral("webtweaks/privacyBlurButton"), true).toBool();

  const QString flags =
      QStringLiteral("{\"dismissExpressionsPanel\":%1,\"themeToggleButton\":%2,"
                     "\"privacyBlurButton\":%3}")
          .arg(QLatin1String(jsBool(dismiss)), QLatin1String(jsBool(themeButton)),
               QLatin1String(jsBool(blurButton)));

  // The injected buttons' accessible labels, translated. QObject::tr with an
  // explicit "WebTweaks" context so they land in the translation catalogue.
  const QString labels =
      QStringLiteral("{\"switchToLight\":%1,\"switchToDark\":%2,"
                     "\"showChats\":%3,\"blurChats\":%4}")
          .arg(jsString(QObject::tr("Switch to light theme", "WebTweaks")),
               jsString(QObject::tr("Switch to dark theme", "WebTweaks")),
               jsString(QObject::tr("Show the chats", "WebTweaks")),
               jsString(QObject::tr("Blur the chats", "WebTweaks")));

  QString source = QString::fromLatin1(kScriptTemplate);
  source.replace(QLatin1String("__FLAGS__"), flags);
  source.replace(QLatin1String("__LABELS__"), labels);
  return source;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  for (const char *name : {kScriptName, kHdFlagScriptName}) {
    const auto existing = scripts->find(QLatin1String(name));
    for (const auto &script : existing)
      scripts->remove(script);
  }

  // Not a user tweak: the HD receive-flag script is always injected (see the
  // comment on kHdFlagScript above).
  QWebEngineScript hdFlag;
  hdFlag.setName(QLatin1String(kHdFlagScriptName));
  hdFlag.setSourceCode(QString::fromLatin1(kHdFlagScript));
  hdFlag.setInjectionPoint(QWebEngineScript::DocumentReady);
  hdFlag.setWorldId(QWebEngineScript::MainWorld);
  hdFlag.setRunsOnSubFrames(false);
  scripts->insert(hdFlag);

  QSettings &s = SettingsManager::instance().settings();
  const bool dismiss =
      s.value(QStringLiteral("webtweaks/dismissExpressionsPanel"), false).toBool();
  const bool themeButton =
      s.value(QStringLiteral("webtweaks/themeToggleButton"), true).toBool();
  const bool blurButton =
      s.value(QStringLiteral("webtweaks/privacyBlurButton"), true).toBool();
  if (!dismiss && !themeButton && !blurButton)
    return; // nothing enabled → do not inject on fresh loads

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace WebTweaks
