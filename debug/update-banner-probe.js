// Whatly probe: capture WhatsApp Web's "Refresh to update" banner when it appears.
//
// Passive. It never clicks, never styles, never removes anything. It watches the
// left pane and, the first time something turns up there that is not a chat row
// but does carry an icon and a sentence, it writes that element's full shape into
// localStorage under __whatly_banner_probe and warns it to the console.
//
// Why a watcher and not a one-shot read: the banner is not there at load. It
// appears only once WhatsApp's service worker has a new version waiting, which
// can be hours or days after the page opened. A reload dismisses it, so it has to
// be caught in place, whenever it happens, without anyone being at the keyboard.
(function () {
  'use strict';
  var KEY = '__whatly_banner_probe';
  if (window.__whatlyBannerProbe) return;
  window.__whatlyBannerProbe = true;

  function attrs(e) {
    var o = {};
    for (var i = 0; i < e.attributes.length; i++) {
      var a = e.attributes[i];
      o[a.name] = a.value.length > 120 ? a.value.slice(0, 120) + '…' : a.value;
    }
    return o;
  }

  // The ancestor chain matters as much as the element: whether the banner sits
  // inside #pane-side or beside it decides which CSS can reach it.
  function chain(e) {
    var out = [], n = e;
    while (n && n !== document.body && out.length < 8) {
      out.push({ tag: n.tagName, attrs: attrs(n) });
      n = n.parentElement;
    }
    return out;
  }

  function describe(e) {
    var icons = [], i;
    var svgs = e.querySelectorAll('svg, [data-icon]');
    for (i = 0; i < svgs.length && i < 8; i++) {
      var t = svgs[i].querySelector ? svgs[i].querySelector('title') : null;
      icons.push({
        tag: svgs[i].tagName,
        dataIcon: svgs[i].getAttribute && svgs[i].getAttribute('data-icon'),
        testid: svgs[i].getAttribute && svgs[i].getAttribute('data-testid'),
        title: t && t.textContent,
        attrs: attrs(svgs[i])
      });
    }
    var acts = [];
    var cs = e.querySelectorAll('button, [role="button"], a, [tabindex]');
    for (i = 0; i < cs.length && i < 8; i++) {
      acts.push({
        tag: cs[i].tagName,
        text: (cs[i].textContent || '').trim().slice(0, 60),
        attrs: attrs(cs[i])
      });
    }
    return {
      when: new Date().toISOString(),
      tag: e.tagName,
      text: (e.textContent || '').trim().slice(0, 300),
      attrs: attrs(e),
      icons: icons,
      clickables: acts,
      chain: chain(e),
      // Capped deliberately: this is WhatsApp's own banner, and the cap keeps
      // anything from a neighbouring chat row out of the capture.
      outerHTML: (e.outerHTML || '').slice(0, 6000)
    };
  }

  // A candidate is: NOT a chat row and not inside one, not holding any (which
  // rules out the list container and every wrapper above it), carrying an icon,
  // carrying a sentence rather than a word, and carrying something to click.
  //
  // The exclusions below are not guesses. The first version of this probe was
  // scoped to #side and captured the FILTER TABLIST on Gert's own page —
  // role="tablist", aria-label="chat-list-filters", text "AllUnread4Favourites
  // Groups2…" — and because sweep() stopped at its first find it would have
  // recorded that for ever and never seen the banner it was written for. The
  // instrument was hiding the answer.
  function excluded(e) {
    if (e.getAttribute('role') === 'tablist') return true;
    if (e.querySelector('[role="tablist"]')) return true;
    if (e.querySelector('#all-filter, #additional-filters')) return true;
    if (e.querySelector('input, [contenteditable="true"], [role="textbox"]')) return true;
    if (e.querySelector('[role="row"]')) return true;    // the list itself
    if (e.querySelector('[role="grid"]')) return true;
    return false;
  }

  function candidate(e) {
    if (!e || e.nodeType !== 1) return null;
    if (e.closest('[role="row"]')) return null;
    if (e.closest('header')) return null;
    if (!e.querySelector('svg, [data-icon]')) return null;
    if (!e.querySelector('button, [role="button"], a')) return null;
    var txt = (e.textContent || '').trim();
    if (txt.length < 12 || txt.length > 400) return null;
    if (excluded(e)) return null;
    return e;
  }

  // A LIST, not a single value: if this ever matches something unintended
  // again, the wrong capture must not stop the right one being recorded.
  function record(e, where) {
    try {
      var d = describe(e);
      d.where = where;
      var all;
      try { all = JSON.parse(localStorage.getItem(KEY) || '[]'); } catch (x) { all = []; }
      if (!all || all.constructor !== Array) all = [];
      for (var i = 0; i < all.length; i++)
        if (all[i] && all[i].text === d.text) return;    // already have this one
      all.push(d);
      while (all.length > 4) all.shift();
      localStorage.setItem(KEY, JSON.stringify(all));
      console.warn('[whatly-banner-probe] CAPTURED (' + where + ') ' + JSON.stringify(d));
    } catch (err) {
      try { console.warn('[whatly-banner-probe] failed: ' + err); } catch (e2) {}
    }
  }

  function sweep() {
    // #pane-side first, because that is where a notice sits and where the strip
    // acts on it. Only if nothing matches there is the rest of #side tried, so a
    // notice WhatsApp puts somewhere unexpected is still recorded rather than
    // silently missed — but it can never mask one in the list.
    var roots = [document.querySelector('#pane-side'),
                 document.querySelector('#side')];
    for (var r = 0; r < roots.length; r++) {
      if (!roots[r]) continue;
      var all = roots[r].querySelectorAll('div, section, aside');
      for (var i = 0; i < all.length; i++) {
        var c = candidate(all[i]);
        if (c) { record(c, r === 0 ? 'pane-side' : 'side'); return; }
      }
    }
  }

  // A sweep now, then a slow poll. There was a MutationObserver here as well,
  // on document.documentElement with subtree:true, and it was a mistake: it
  // fired on WhatsApp's constant DOM churn and ran a querySelectorAll over the
  // whole side pane each time. Whatly's own strip code refuses to do this and
  // says why -- "a MutationObserver over a tree WhatsApp mutates continuously
  // would measure the layout several times a second instead - that cost 40% of
  // a core once already". Measured here on 2026sep01 it was polluting the very
  // frame timings the scroll probe was trying to read. The initial sweep covers
  // a notice already on the page; the poll catches one that appears later,
  // within five seconds, which is nothing next to how long a notice sits there.
  setInterval(sweep, 5000);
  sweep();
  console.warn('[whatly-banner-probe] armed');
})();
