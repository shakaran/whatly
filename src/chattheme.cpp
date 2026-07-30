#include "chattheme.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-chat-theme";
static const char kSettingsKey[] = "chatTheme";

// The recolouring runs in the page, over the page's own stylesheets, because
// only the page can read them. __PARAMS__ becomes a JSON object.
//
// Two decisions here were arrived at by measuring the live page, and both are
// easy to get wrong:
//
//  * Colourfulness is judged by CHROMA (max-min), not by HSL saturation. Near
//    white and near black, HSL saturation explodes: WhatsApp's cream
//    background #F5F1EB reports 33% saturation while being, to the eye, a grey.
//    Keyed on saturation it was mistaken for a colour, found not to be green,
//    and left untouched — which is exactly why the light theme stayed cream.
//
//  * The green band starts at 88°, not 120°. The outgoing-bubble green of the
//    light theme (#d9fdd3) sits at 111°, and a band starting at 120° misses the
//    single most visible surface in the app.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  var P = __PARAMS__;
  var STYLE_ID = 'whatly-chat-theme';

  var parse = function (v) {
    var m = /^#([0-9a-f]{6})$/i.exec(v);
    if (m) { var n = parseInt(m[1], 16);
             return {r: n >> 16 & 255, g: n >> 8 & 255, b: n & 255, a: 1}; }
    m = /^#([0-9a-f]{3})$/i.exec(v);
    if (m) { var h = m[1];
             return {r: parseInt(h[0] + h[0], 16), g: parseInt(h[1] + h[1], 16),
                     b: parseInt(h[2] + h[2], 16), a: 1}; }
    m = /^rgba?\(\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)(?:[,\s\/]+([\d.]+))?\s*\)$/i.exec(v);
    if (m) return {r: +m[1], g: +m[2], b: +m[3],
                   a: m[4] === undefined ? 1 : +m[4]};
    // Bare RGB channels, e.g. "20, 77, 55" — WhatsApp's design-system tokens
    // (--WDS-*-RGB) store colours this way for use inside rgb(var(--x) / a). The
    // caller only trusts this for a variable whose name marks it as RGB, and
    // must re-emit it as bare channels, not as hsl(). Without this every
    // --*-RGB colour was skipped, so the outgoing-bubble green that the message
    // options button fades over stayed green under a themed (pink) bubble.
    m = /^(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})$/.exec(v);
    if (m) return {r: +m[1], g: +m[2], b: +m[3], a: 1, channels: true};
    return null;   // var() alias, gradient, or not a colour at all
  };

  var hsl = function (c) {
    var R = c.r / 255, G = c.g / 255, B = c.b / 255;
    var max = Math.max(R, G, B), min = Math.min(R, G, B);
    var l = (max + min) / 2, d = max - min;
    if (!d) return {h: 0, s: 0, l: l, c: 0};
    var s = d / (1 - Math.abs(2 * l - 1));
    var h = max === R ? 60 * (((G - B) / d) % 6)
          : max === G ? 60 * (((B - R) / d) + 2)
                      : 60 * (((R - G) / d) + 4);
    if (h < 0) h += 360;
    return {h: h, s: s, l: l, c: d};
  };

  var css = function (h, s, l, a) {
    var body = h.toFixed(0) + ' ' + (s * 100).toFixed(1) + '% ' + (l * 100).toFixed(1) + '%';
    return a === 1 ? 'hsl(' + body + ')' : 'hsl(' + body + ' / ' + a + ')';
  };

  // For --*-RGB tokens the value must go back as bare "r, g, b" channels, since
  // the page consumes them as rgb(var(--x) / a); an hsl() there would be invalid.
  var hslToRgb = function (h, s, l) {
    h = ((h % 360) + 360) % 360;
    var C = (1 - Math.abs(2 * l - 1)) * s, X = C * (1 - Math.abs((h / 60) % 2 - 1)), m = l - C / 2;
    var r = 0, g = 0, b = 0;
    if (h < 60) { r = C; g = X; } else if (h < 120) { r = X; g = C; }
    else if (h < 180) { g = C; b = X; } else if (h < 240) { g = X; b = C; }
    else if (h < 300) { r = X; b = C; } else { r = C; b = X; }
    return (Math.round((r + m) * 255)) + ', ' + (Math.round((g + m) * 255)) +
           ', ' + (Math.round((b + m) * 255));
  };

  var apply = function () {
    var rules = [];
    var collect = function (list) {
      for (var i = 0; i < list.length; i++) {
        var rule = list[i];
        rules.push(rule);
        // @media / @layer / @supports wrap their children; a scan that only
        // looks at the top level misses every variable declared inside them.
        if (rule.cssRules && rule.cssRules.length) collect(rule.cssRules);
      }
    };
    for (var s = 0; s < document.styleSheets.length; s++) {
      var sheet = document.styleSheets[s];
      if (sheet.ownerNode && sheet.ownerNode.id === STYLE_ID) continue;  // never read our own output back
      try { collect(sheet.cssRules); } catch (e) { /* cross-origin */ }
    }

    var blocks = [];
    for (var r = 0; r < rules.length; r++) {
      var rule = rules[r];
      if (!rule.style || !rule.selectorText) continue;
      var decls = [];
      for (var d = 0; d < rule.style.length; d++) {
        var name = rule.style[d];
        if (name.indexOf('--') !== 0) continue;
        var rgb = parse(rule.style.getPropertyValue(name).trim());
        if (!rgb) continue;
        // A bare numeric triple is only a colour when the token name says so
        // (--*-RGB); otherwise it could be anything (spacing, a ratio, ...).
        if (rgb.channels && name.toLowerCase().indexOf('rgb') < 0) continue;
        var c = hsl(rgb);
        var out = null; // {h,s,l,a} of the themed colour, or null to leave it
        if (c.c < P.neutralChroma) {
          if (c.l <= 0.02) continue;                       // pure black: a tint would not show
          out = {h: P.hue, s: P.neutralTint,
                 l: Math.min(c.l, P.lightnessCeiling), a: rgb.a};
        } else if (c.h >= P.greenLo && c.h <= P.greenHi) {
          var l = c.l < 0.5 ? Math.min(c.l + P.accentLift, 0.72) : c.l;
          out = {h: P.hue, s: Math.min(c.s, P.accentSat), l: l, a: rgb.a};
        }
        // Everything else — link blue, warning red — keeps its meaning.
        if (!out) continue;
        decls.push(name + ':' +
            (rgb.channels ? hslToRgb(out.h, out.s, out.l)
                          : css(out.h, out.s, out.l, out.a)) +
            ' !important');
      }
      if (decls.length)
        blocks.push(rule.selectorText + '{' + decls.join(';') + '}');
    }

    var el = document.getElementById(STYLE_ID);
    if (!blocks.length) { if (el) el.remove(); return; }
    if (!el) {
      el = document.createElement('style');
      el.id = STYLE_ID;
      (document.head || document.documentElement).appendChild(el);
    }
    el.textContent = blocks.join('');
  };

  var run = function () { try { apply(); } catch (e) { /* never break the page */ } };
  run();

  // WhatsApp loads stylesheets lazily and swaps its palette when the light/dark
  // theme changes, so recompute rather than assume one pass is enough.
  if (!window.__whatlyChatThemeWatching) {
    window.__whatlyChatThemeWatching = true;
    var pending = null;
    var schedule = function () {
      clearTimeout(pending);
      pending = setTimeout(function () {
        if (window.__whatlyChatThemeApply) window.__whatlyChatThemeApply();
      }, 250);
    };
    new MutationObserver(schedule).observe(document.documentElement, {
      attributes: true, attributeFilter: ['class'],   // light ⇄ dark
    });
    new MutationObserver(function (records) {
      for (var i = 0; i < records.length; i++) {
        var added = records[i].addedNodes;
        for (var j = 0; j < added.length; j++) {
          var tag = added[j].tagName;
          if (tag === 'STYLE' || tag === 'LINK') { schedule(); return; }
        }
      }
    }).observe(document.documentElement, {childList: true, subtree: true});
  }
  window.__whatlyChatThemeApply = run;   // always the newest theme's parameters
})();
)JS";

namespace ChatTheme {

QList<Theme> themes() {
  // hue, accent-saturation ceiling, neutral tint.
  return {
      {QStringLiteral("none"),
       QCoreApplication::translate("ChatTheme", "WhatsApp (default)"), 0, 0, 0},
      {QStringLiteral("barbie"),
       QCoreApplication::translate("ChatTheme", "Barbie pink"), 335, 0.78, 0.62},
      {QStringLiteral("rose"),
       QCoreApplication::translate("ChatTheme", "Dusty rose"), 345, 0.52, 0.30},
      {QStringLiteral("lavender"),
       QCoreApplication::translate("ChatTheme", "Lavender"), 270, 0.55, 0.34},
      {QStringLiteral("violet"),
       QCoreApplication::translate("ChatTheme", "Violet"), 292, 0.70, 0.40},
      {QStringLiteral("sky"),
       QCoreApplication::translate("ChatTheme", "Sky blue"), 205, 0.62, 0.32},
      {QStringLiteral("ocean"),
       QCoreApplication::translate("ChatTheme", "Deep ocean"), 222, 0.70, 0.38},
      {QStringLiteral("teal"),
       QCoreApplication::translate("ChatTheme", "Teal"), 186, 0.60, 0.30},
      {QStringLiteral("mint"),
       QCoreApplication::translate("ChatTheme", "Mint"), 152, 0.45, 0.24},
      {QStringLiteral("coral"),
       QCoreApplication::translate("ChatTheme", "Coral"), 8, 0.66, 0.34},
      {QStringLiteral("peach"),
       QCoreApplication::translate("ChatTheme", "Peach"), 24, 0.58, 0.30},
      {QStringLiteral("gold"),
       QCoreApplication::translate("ChatTheme", "Gold"), 44, 0.62, 0.28},
      {QStringLiteral("crimson"),
       QCoreApplication::translate("ChatTheme", "Crimson"), 352, 0.72, 0.36},
      {QStringLiteral("graphite"),
       QCoreApplication::translate("ChatTheme", "Graphite"), 220, 0.16, 0.06},
  };
}

QString currentThemeId() {
  const QString id = SettingsManager::instance()
                         .settings()
                         .value(QLatin1String(kSettingsKey),
                                QStringLiteral("none"))
                         .toString();
  for (const Theme &theme : themes())
    if (theme.id == id)
      return id;
  return QStringLiteral("none"); // a theme that no longer exists
}

void setCurrentThemeId(const QString &id) {
  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  id);
}

QString scriptSource() {
  const QString id = currentThemeId();
  if (id == QLatin1String("none"))
    return QString();

  Theme theme = themes().first();
  for (const Theme &candidate : themes())
    if (candidate.id == id)
      theme = candidate;

  const QString params =
      QStringLiteral("{\"hue\":%1,\"accentSat\":%2,\"neutralTint\":%3,"
                     "\"accentLift\":0.12,\"neutralChroma\":0.14,"
                     "\"lightnessCeiling\":0.945,"
                     "\"greenLo\":88,\"greenHi\":195}")
          .arg(theme.hue)
          .arg(theme.accentSat)
          .arg(theme.neutralTint);

  QString source = QString::fromLatin1(kScriptTemplate);
  source.replace(QLatin1String("__PARAMS__"), params);
  return source;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  const QString source = scriptSource();
  if (source.isEmpty())
    return; // "none" → WhatsApp's own colours

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(source);
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace ChatTheme
