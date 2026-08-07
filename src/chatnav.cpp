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
  var tries = 0;
  var attempt = function () {
    var pane = document.querySelector('#pane-side');
    var rows = pane ? pane.querySelectorAll('[role="row"]') : [];
    var el = null;
    for (var i = 0; i < rows.length; i++) {
      var n = rows[i].querySelector('span[title]');
      if (n && n.getAttribute('title') === NAME) { el = n; break; }
    }
    if (!el) {
      // Keep asking for a few seconds rather than deciding on the first look.
      // This also runs against a page that has only just been built — for an
      // account that had none until its chat was picked out of the tray — and
      // such a page has no chat list yet, then fills it in progressively.
      if (++tries < 20) { setTimeout(attempt, 250); return 'waiting'; }
      return pane ? 'not-found' : 'no-pane';
    }
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
  };
  return attempt();
})()
)JS").arg(jsonString(name));
}

QString focusSearchScript() {
  return QStringLiteral(R"JS(
(function(){
  // Ctrl+F means "find in what I am looking at". With a conversation open, that
  // is the conversation — WhatsApp's own search-within-chat, the magnifier in the
  // chat header — and not the chat list's search box, which is a different
  // question entirely. The list search is what it falls back to when there is no
  // conversation open to search.
  var tries = 0;
  function vis(e) {
    var r = e.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  function focusIn(el) {
    el.focus();
    // Select what is already there, so typing replaces the old term rather than
    // appending to it. Not every element type has select().
    if (typeof el.select === 'function') { try { el.select(); } catch (e) {} }
    else {
      try {
        var r = document.createRange();
        r.selectNodeContents(el);
        var s = window.getSelection();
        s.removeAllRanges();
        s.addRange(r);
      } catch (e) {}
    }
  }
  // The magnifier in the open conversation's header. data-icon is WhatsApp's own
  // attribute and survives their builds; every class around it is obfuscated and
  // does not. The aria-label pass is for builds that drop the attribute, and it
  // can only cover the languages it names, which is why it is second.
  function headerSearch() {
    var main = document.querySelector('#main');
    var header = main && main.querySelector('header');
    if (!header) return null;
    var icon = header.querySelector('[data-icon^="search"]');
    if (icon) return icon.closest('button') || icon;
    var btns = header.querySelectorAll('button');
    for (var i = 0; i < btns.length; i++) {
      var l = (btns[i].getAttribute('aria-label') || '').toLowerCase();
      if (/search|buscar|recherch|such|cerca|procur|ricerc|zoek|поиск|szukaj|serĉ/.test(l))
        return btns[i];
    }
    return null;
  }
  // The field the search panel opens with: it is in neither the chat list
  // (#side) nor the conversation (#main), because WhatsApp puts the panel in its
  // own pane beside them. That places it without naming a single class, and it
  // is also what keeps this off the message composer, which lives inside #main.
  function panelBox() {
    var all = document.querySelectorAll(
      'input,[contenteditable="true"],[role="textbox"]');
    for (var i = 0; i < all.length; i++) {
      var e = all[i];
      if (e.closest('#side') || e.closest('#main')) continue;
      if (vis(e)) return e;
    }
    return null;
  }
  // A plain click is ignored by parts of this UI, so send the sequence it does
  // react to, the same one the chat-opening script uses.
  function press(el) {
    var r = el.getBoundingClientRect();
    var o = { bubbles: true, cancelable: true, view: window,
              clientX: r.x + r.width / 2, clientY: r.y + r.height / 2,
              button: 0, pointerId: 1, pointerType: 'mouse', isPrimary: true };
    el.dispatchEvent(new PointerEvent('pointerdown', o));
    el.dispatchEvent(new MouseEvent('mousedown', o));
    el.dispatchEvent(new PointerEvent('pointerup', o));
    el.dispatchEvent(new MouseEvent('mouseup', o));
    el.dispatchEvent(new MouseEvent('click', o));
  }
  function waitForPanel() {
    var b = panelBox();
    if (b) { focusIn(b); return 'chat-search'; }
    if (++tries < 15) { setTimeout(waitForPanel, 100); return 'waiting'; }
    return 'chat-search-no-field';
  }

  var btn = headerSearch();
  if (btn) { press(btn); return waitForPanel(); }

  // No conversation open. The chat list's own search box is the sensible answer,
  // named by aria-label in the languages we can name and found structurally
  // otherwise — the one input inside #side — so the key is not dead elsewhere.
  function listBox() {
    return document.querySelector('input[aria-label*="Search" i]')
      || document.querySelector('input[aria-label*="Buscar" i]')
      || document.querySelector('input[data-tab="3"]')
      || document.querySelector('#side [contenteditable="true"]')
      || document.querySelector('#side [role="textbox"]')
      || document.querySelector('#side input');
  }
  function attemptList() {
    var b = listBox();
    if (b && vis(b)) { focusIn(b); return 'list-search'; }
    if (++tries < 15) { setTimeout(attemptList, 150); return 'waiting'; }
    return 'not-found';
  }
  // Collapsed, that box is clipped to a sliver, so focusing it is no use: open
  // the list first, which is what a click up there does. Same test the sidebar
  // buttons use for the collapsed state. Expanding is a round trip through the
  // app, hence the retry.
  var strip = document.getElementById('whatly-chatlist-strip');
  if (strip && strip.textContent && window.__whatlyBridge &&
      window.__whatlyBridge.toggleChatListStrip)
    window.__whatlyBridge.toggleChatListStrip();
  return attemptList();
})()
)JS");
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
