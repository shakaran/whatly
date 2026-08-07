#include "chatnav.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {
// A JSON string literal for embedding a value safely in the script: serialise a
// one-element array and strip the brackets, leaving a properly-escaped "…".
QString jsonString(const QString &s) {
  const QByteArray a =
      QJsonDocument(QJsonArray{QJsonValue(s)}).toJson(QJsonDocument::Compact);
  const QString r = QString::fromUtf8(a).trimmed(); // ["…"]
  return r.mid(1, r.length() - 2);
}
} // namespace

namespace ChatNav {

QString unreadChatsScript(int limit) {
  if (limit < 1)
    limit = 1;
  return QStringLiteral(R"JS(
(function(){
  var pane = document.querySelector('#pane-side');
  if (!pane) return '[]';
  var rows = pane.querySelectorAll('[role="row"]');
  var out = [];
  for (var i = 0; i < rows.length; i++) {
    var row = rows[i];
    var nameEl = row.querySelector('span[title]');
    if (!nameEl) continue;
    // The unread badge is a leaf span whose whole text is the count; timestamps
    // contain ":" so they are excluded. Locale-independent.
    var count = 0;
    var spans = row.querySelectorAll('span');
    for (var j = 0; j < spans.length; j++) {
      var t = (spans[j].textContent || '').trim();
      if (spans[j].children.length === 0 && /^\d{1,4}$/.test(t)) count = parseInt(t, 10);
    }
    if (count > 0) {
      out.push({ name: nameEl.getAttribute('title'), count: count });
      if (out.length >= %1) break;
    }
  }
  return JSON.stringify(out);
})()
)JS").arg(limit);
}

QString openChatByNameScript(const QString &name) {
  return QStringLiteral(R"JS(
(function(){
  var NAME = %1;
  var pane = document.querySelector('#pane-side');
  if (!pane) return 'no-pane';
  var rows = pane.querySelectorAll('[role="row"]');
  var el = null;
  for (var i = 0; i < rows.length; i++) {
    var n = rows[i].querySelector('span[title]');
    if (n && n.getAttribute('title') === NAME) { el = n; break; }
  }
  if (!el) return 'not-found';
  var r = el.getBoundingClientRect();
  var cx = r.x + r.width / 2, cy = r.y + r.height / 2;
  var opts = { bubbles: true, cancelable: true, view: window,
               clientX: cx, clientY: cy, button: 0,
               pointerId: 1, pointerType: 'mouse', isPrimary: true };
  el.dispatchEvent(new PointerEvent('pointerdown', opts));
  el.dispatchEvent(new MouseEvent('mousedown', opts));
  el.dispatchEvent(new PointerEvent('pointerup', opts));
  el.dispatchEvent(new MouseEvent('mouseup', opts));
  el.dispatchEvent(new MouseEvent('click', opts));
  return 'ok';
})()
)JS").arg(jsonString(name));
}

QString focusSearchScript() {
  return QStringLiteral(R"JS(
(function(){
  var tries = 0;
  function box() {
    // Named first, because it is unambiguous — but only in the languages we can
    // name, so the structural fallbacks below carry every other locale: the
    // search field is the one text input inside #side and above the chat list.
    return document.querySelector('input[aria-label*="Search" i]')
      || document.querySelector('input[aria-label*="Buscar" i]')
      || document.querySelector('input[data-tab="3"]')
      || document.querySelector('#side [contenteditable="true"]')
      || document.querySelector('#side [role="textbox"]')
      || document.querySelector('#side input');
  }
  function attempt() {
    var b = box();
    if (b) {
      b.focus();
      // Select what is already there, so typing replaces the old term instead of
      // appending to it. Not every element type has select().
      if (typeof b.select === 'function') { try { b.select(); } catch (e) {} }
      else {
        try {
          var r = document.createRange();
          r.selectNodeContents(b);
          var s = window.getSelection();
          s.removeAllRanges();
          s.addRange(r);
        } catch (e) {}
      }
      return 'ok';
    }
    // The page may still be building, and the chat list may have just been
    // expanded on our own account, which is a round trip through the app.
    if (++tries < 12) { setTimeout(attempt, 150); return 'waiting'; }
    return 'not-found';
  }
  return attempt();
})()
)JS");
}

} // namespace ChatNav
