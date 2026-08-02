#include "undosend.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QObject>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-undo-send";
static const char kEnabledKey[] = "undoSendEnabled";
static const char kSecondsKey[] = "undoSendSeconds";

namespace {
// A JSON string literal for safe embedding in the script.
QString jsStr(const QString &s) {
  const QByteArray a =
      QJsonDocument(QJsonArray{QJsonValue(s)}).toJson(QJsonDocument::Compact);
  const QString r = QString::fromUtf8(a).trimmed(); // ["…"]
  return r.mid(1, r.length() - 2);
}
} // namespace

// __ON__ true/false, __SECS__ integer, __UNDO__ / __SENDING__ JSON strings.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  try {
    if (window.__whatlyUndoSend) {              // already installed: just retune
      window.__whatlyUndoSend.enabled = __ON__;
      window.__whatlyUndoSend.secs = __SECS__;
      return;
    }
    var S = { enabled: __ON__, secs: __SECS__, bypass: false,
              timer: null, iv: null, toast: null };
    window.__whatlyUndoSend = S;
    var UNDO = __UNDO__, SENDING = __SENDING__;

    function clearToast() {
      if (S.iv) { clearInterval(S.iv); S.iv = null; }
      if (S.timer) { clearTimeout(S.timer); S.timer = null; }
      if (S.toast) { S.toast.remove(); S.toast = null; }
    }
    function sendNow(box) {
      clearToast();
      S.bypass = true;
      try {
        box.focus();
        box.dispatchEvent(new KeyboardEvent('keydown',
          { key: 'Enter', code: 'Enter', keyCode: 13, which: 13,
            bubbles: true, cancelable: true }));
      } catch (e) {}
      S.bypass = false;
    }
    function showToast(box, secs) {
      clearToast();
      var t = document.createElement('div');
      t.id = 'whatly-undo-toast';
      t.style.cssText = 'position:fixed;bottom:84px;left:50%;transform:translateX(-50%);' +
        'z-index:100000;background:#202c33;color:#e9edef;padding:8px 16px;border-radius:20px;' +
        'box-shadow:0 4px 16px rgba(0,0,0,.45);font-size:14px;display:flex;gap:14px;align-items:center;';
      var span = document.createElement('span');
      var left = secs;
      span.textContent = SENDING + ' ' + left;
      var btn = document.createElement('button');
      btn.textContent = UNDO;
      btn.style.cssText = 'background:transparent;border:0;color:#00a884;font-weight:700;' +
        'cursor:pointer;font-size:14px;padding:0;';
      btn.onclick = function () { clearToast(); }; // text stays in the composer
      t.appendChild(span); t.appendChild(btn);
      (document.body || document.documentElement).appendChild(t);
      S.toast = t;
      S.iv = setInterval(function () {
        left--; if (left > 0) span.textContent = SENDING + ' ' + left;
      }, 1000);
      S.timer = setTimeout(function () { sendNow(box); }, secs * 1000);
    }

    document.addEventListener('keydown', function (e) {
      if (!S.enabled) return;
      if (e.key !== 'Enter' || e.shiftKey || e.ctrlKey || e.altKey ||
          e.metaKey || e.isComposing) return;
      if (S.bypass) return;                     // our own programmatic send
      var box = e.target && e.target.closest &&
                e.target.closest("#main footer [contenteditable='true']");
      if (!box) return;
      if (!(box.innerText || '').trim()) return; // nothing to hold
      e.preventDefault();
      e.stopImmediatePropagation();
      if (S.timer) { sendNow(box); return; }    // a second Enter means send now
      showToast(box, S.secs);
    }, true);                                    // capture: run before WhatsApp's
  } catch (e) { /* never break the page */ }
})();
)JS";

namespace UndoSend {

bool isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEnabledKey), false)
      .toBool();
}
int seconds() {
  const int s = SettingsManager::instance()
                    .settings()
                    .value(QLatin1String(kSecondsKey), 5)
                    .toInt();
  return s < 1 ? 1 : s;
}
void setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEnabledKey),
                                                  enabled);
}
void setSeconds(int secs) {
  SettingsManager::instance().settings().setValue(QLatin1String(kSecondsKey),
                                                  secs < 1 ? 1 : secs);
}

QString scriptSource() {
  QString src = QString::fromLatin1(kScriptTemplate);
  src.replace(QLatin1String("__ON__"),
              isEnabled() ? QLatin1String("true") : QLatin1String("false"));
  src.replace(QLatin1String("__SECS__"), QString::number(seconds()));
  src.replace(QLatin1String("__UNDO__"),
              jsStr(QObject::tr("Undo")));
  src.replace(QLatin1String("__SENDING__"),
              jsStr(QObject::tr("Sending in")));
  return src;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace UndoSend
