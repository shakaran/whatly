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

QString unreadSummaryScript() {
  return QStringLiteral(R"JS(
(function(){
  // Counting the drawn rows, for when the database cannot be read. Same badge
  // detection as unreadChatsScript: a leaf span whose whole text is a number.
  function fromList(){
    var pane = document.querySelector('#pane-side');
    if (!pane) return null;
    var rows = pane.querySelectorAll('[role="row"]');
    var chats = 0, messages = 0;
    for (var i = 0; i < rows.length; i++) {
      var spans = rows[i].querySelectorAll('span'), n = 0;
      for (var j = 0; j < spans.length; j++) {
        var t = (spans[j].textContent || '').trim();
        if (spans[j].children.length === 0 && /^\d{1,4}$/.test(t)) n = parseInt(t, 10);
      }
      if (n > 0) { chats++; messages += n; }
    }
    return { chats: chats, messages: messages, source: 'list' };
  }

  // runJavaScript does not await a promise — it would hand back the promise
  // object itself — and reading a database cannot be anything but asynchronous.
  // So the answer is kept on the page: every call returns what was last worked
  // out and starts the next read, and with a call every few seconds the number
  // is at most one beat behind. Until the first read lands, the drawn rows
  // answer, which is right for the top of the list and wrong only for what has
  // scrolled out of it.
  async function fromDatabase(){
    if (!indexedDB.databases) return null;
    var dbs = await indexedDB.databases();
    var name = null;
    for (var i = 0; i < dbs.length; i++)
      if (dbs[i].name === 'model-storage') name = dbs[i].name;
    if (!name) return null;

    var db = await new Promise(function(resolve, reject){
      var r = indexedDB.open(name);
      r.onsuccess = function(){ resolve(r.result); };
      r.onerror = function(){ reject(r.error); };
      r.onblocked = function(){ reject(new Error('blocked')); };
    });
    if (!db.objectStoreNames.contains('chat')) { db.close(); return null; }

    // A cursor rather than getAll(): this runs every few seconds, and getAll on
    // a chat store of several hundred records deserialises every field of every
    // one of them to read two.
    // `walked` is the difference between "nothing is unread" and "the read did
    // not finish". Resolving the error paths with the counters as they stand
    // would report a confident zero and clear every badge.
    var chats = 0, messages = 0, walked = false;
    await new Promise(function(resolve){
      var tx = db.transaction('chat', 'readonly');
      var req = tx.objectStore('chat').openCursor();
      req.onsuccess = function(){
        var cur = req.result;
        if (!cur) { walked = true; resolve(); return; }
        var c = cur.value || {};
        var n = c.unreadCount | 0;
        // A chat marked unread by hand has no messages to count, and WhatsApp
        // records it as a negative count or as a flag depending on the build.
        // It belongs in the total either way: it carries a pill in the list and
        // it appears under the list's own "unread" filter, which is the set this
        // number is meant to be the size of.
        var marked = n < 0 || !!c.markedUnread;
        if ((n > 0 || marked) && !c.archive) {
          chats++;
          messages += (n > 0 ? n : 0);
        }
        cur.continue();
      };
      req.onerror = function(){ resolve(); };
      tx.onabort = function(){ resolve(); };
    });
    db.close();
    return walked ? { chats: chats, messages: messages, source: 'db' } : null;
  }

  var W = window.__whatlyUnread ||
          (window.__whatlyUnread = { known: null, busy: false, at: 0 });
  // Asked often — a title change is not the only way an unread count moves, and
  // marking a chat read or unread by hand moves it without one — so the read
  // itself is throttled here rather than at the call site. Calls in between are
  // free: they hand back the number already worked out.
  if (!W.busy && Date.now() - (W.at || 0) > 2500) {
    W.busy = true;
    W.at = Date.now();
    fromDatabase()
      .then(function(r){ if (r) W.known = r; })
      .catch(function(){})
      .then(function(){ W.busy = false; });
  }
  return JSON.stringify(W.known || fromList() ||
                        { chats: 0, messages: 0, source: 'none' });
})()
)JS");
}

} // namespace ChatNav
