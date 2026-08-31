#include "chatliststrip.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-chatlist-strip";
static const char kSettingsKey[] = "chatListStrip";
static const char kPreviewSizeKey[] = "chatListStripPreviewSize";

// How wide the collapsed list is: WhatsApp's own 48px avatar plus the 23px and
// 25px of padding its row already draws either side of it — 96px — plus the 1px
// the pane's wrapper carries as padding-left. The wrapper is border-box, so
// asking for 96 leaves 95 of content and clips the avatar block by exactly one
// column.
//
// Chosen so the picture lands centred WITHOUT overriding that padding: the
// elements carrying it are class-named, and every class in WhatsApp Web is
// compiler-generated (.x10tvbhy and the like) and changes with each deploy.
// Measured against a live page; if WhatsApp restyles its rows this becomes
// off-centre, never broken.
static const char kStripWidth[] = "97px";

// How much the hover preview shrinks its content. The clone is laid out at
// (panel width / this), so a smaller number means both smaller text AND more
// room for it before anything wraps.
//
// 0.7 was settled on against Windows' font rendering first, but the same build
// on Linux came back "slightly too small" and Windows agreed once there was a
// control to compare against. Everyone starts on the middle step, and the user
// can override it — this is a matter of eyesight and screen as much as of
// toolkit, and nobody else's measurement settles it.
static const char kPreviewZoomSmall[] = "0.7";
static const char kPreviewZoomMedium[] = "0.85";
static const char kPreviewZoomLarge[] = "1";

// The pane is narrowed and its contents are then simply CLIPPED, rather than
// each unwanted element being selected and hidden — clipping needs to know
// nothing about the name/preview/timestamp nodes, which is the only way to
// survive WhatsApp's generated class names.
//
// NOTE the two things this rule does NOT do, both learned the hard way:
//
//  * It does not name the pane by selector. The width does not live on
//    #pane-side or on #side but on an unnamed wrapper above them — AND the
//    layout carries a SECOND, parallel column of the same width inside the
//    legacy `.two` container, which is what actually pushes the conversation
//    across. Shrinking only the first left a full-height hairline exactly where
//    the list used to end, because the conversation's own border-left never
//    moved. So the columns are found by measurement in prepare() below and
//    tagged, and this rule styles the tag.
//
//  * It does not use the `flex` shorthand. #side sits in a column-direction
//    flex parent, and in the Calls section so does the whole pane column, where
//    `flex: 0 0 97px` sets the HEIGHT and folds the section into a 97px box.
//    Growing/shrinking are pinned instead and the basis left alone, so only the
//    width is ever touched whichever way the parent runs.
static const char kCollapseCss[] =
    "[data-whatly-pane]{flex-grow:0!important;flex-shrink:0!important;"
    "flex-basis:auto!important;width:%1!important;"
    "min-width:%1!important;max-width:%1!important}"
    "#side,#pane-side{overflow-x:hidden!important}"
    "#pane-side [role=\"row\"]{overflow:hidden!important}"
    // The unread count, put back. WhatsApp's own badge sits at the right-hand end
    // of a full-width row, so a 97px column cuts it off — and losing it is losing
    // the one thing the collapsed list most needs to tell you. Redrawn as a pill
    // in the row's top-right corner from the count that markUnread() reads into
    // the attribute, so there is no extra element to create, keep in step or leak
    // when WhatsApp recycles the row. Rows carrying no attribute draw nothing,
    // which is how "absent if zero" falls out for free.
    //
    // NOTE what this must NOT do: give the row `position:relative`. A row here is
    // a virtual-list item, absolutely positioned and translated into place (see
    // the preview note below). Making it relative returns it to normal flow while
    // it keeps its transform, so the rows drift apart down the list and the
    // filtered views appear to lose entries. It is also unnecessary — an
    // absolutely positioned element is already the containing block its own
    // absolutely positioned children are measured against.
    //
    // The colour comes from WhatsApp's own badge, published by markUnread() as a
    // custom property, so it matches whatever the current theme paints rather than
    // being a hard-coded green that only suits one of them.
    "#pane-side [role=\"row\"][data-whatly-unread]::after{"
    "content:attr(data-whatly-unread);position:absolute!important;"
    "top:2px;right:2px;z-index:5;"
    "min-width:24px;height:24px;padding:0 6px;box-sizing:border-box;"
    "border-radius:12px;"
    "background:var(--whatly-unread-bg,#25d366);"
    "color:var(--whatly-unread-fg,#fff);"
    "font-size:15px;font-weight:600;line-height:24px;text-align:center;"
    "pointer-events:none}"
    // WhatsApp's own notices inside the list — today the "Refresh to update"
    // banner it shows once its service worker has a new version waiting. A
    // notice is not a row, so the clip that keeps a row to its avatar does
    // nothing for it: a row holds its name and message on ONE line and is simply
    // cut off at 97px, while a notice's sentence WRAPS, and a 97px column turns
    // it into a ladder of single letters running the height of the strip.
    //
    // Treated as the topmost chat instead, which is what it already looks like:
    // the icon stays where an avatar sits, the words are clipped exactly as a
    // row's are, and the whole notice goes in the hover preview. Nothing is
    // hidden and no size is imposed, so the notice keeps its own background and
    // still reads as not-a-chat.
    "[data-whatly-banner]{overflow:hidden!important}"
    "[data-whatly-banner],[data-whatly-banner] *{cursor:pointer!important}"
    // The rule that does the actual work; everything else here follows from it.
    // The notice's own box is named as well as its descendants: the capture
    // shows its sentences sitting as bare text directly inside it, and a text
    // node has no element of its own for `*` to reach — white-space has to be
    // set on whatever holds it.
    // The preview is a clone and would inherit this and show one clipped line,
    // so show() takes the attribute off the clone.
    "[data-whatly-banner],[data-whatly-banner] *{white-space:nowrap!important;"
    "overflow:hidden!important;text-overflow:clip!important}"
    // The filter pills (All / Unread / Favourites / more) are a horizontal row
    // that a 97px column simply guillotines. Fold them into a 2x2 grid of round
    // buttons instead, each labelled with the first letter of its own caption —
    // taken from the caption itself in prepare(), so it follows the interface
    // language rather than assuming English. They stay the REAL buttons, so
    // clicking one filters, and the "more" popover still anchors to itself and
    // opens without the list having to expand.
    "[data-whatly-filters]{display:grid!important;"
    "grid-template-columns:1fr 1fr!important;gap:4px!important;"
    "padding:4px 6px!important;justify-items:stretch!important;"
    "align-content:start!important;height:auto!important;"
    "overflow:visible!important}"
    "[data-whatly-filters]>*{display:none!important}"
    "[data-whatly-filters]>[data-whatly-cell]{display:block!important;"
    "width:auto!important;min-width:0!important;margin:0!important;"
    "padding:0!important}"
    // Rounded rectangles filling their cell rather than circles floating in it:
    // at 97px, two 28px circles leave more gap between them than they occupy.
    "[data-whatly-filters] button{display:flex!important;"
    "align-items:center!important;justify-content:center!important;"
    "width:100%!important;height:26px!important;min-width:0!important;"
    "max-width:none!important;padding:0!important;margin:0!important;"
    "border-radius:8px!important;overflow:hidden!important;font-size:0!important}"
    "[data-whatly-filters] button>*{display:none!important}"
    "[data-whatly-filters] button::before{content:attr(data-whatly-letter);"
    "font-size:13px!important;font-weight:600!important;line-height:1!important}"
    // The pane's own title bar: "WhatsApp" wordmark and the like, which at this
    // width is a letter and a half. Tagged in prepare().
    "[data-whatly-hide]{display:none!important}"
    // The hover preview is a clone of a row, and a row is a virtual-list item:
    // absolutely positioned, translated into place and given an inline pixel
    // height. Cloned into a panel of its own it would sit out of flow and leave
    // that panel zero pixels tall, so put it back in normal flow and let its
    // height come from its content.
    "#whatly-chatlist-tip>*{position:static!important;transform:none!important;"
    "height:auto!important;width:auto!important;z-index:auto!important;"
    "transition:none!important}"
    // It also inherits the truncation the list uses to keep every chat on one
    // line. In a preview there is room to read, so let the text wrap instead of
    // ending in an ellipsis.
    "#whatly-chatlist-tip *{white-space:normal!important;"
    "text-overflow:clip!important}"
    // Keep the picture in the top-left corner. Letting the text wrap (just above)
    // is what moved it: the row centres the avatar against its text column, and a
    // wrapped message makes that column several lines tall, so the avatar drifts
    // down — far enough with a long message to leave the panel altogether, which
    // is why it sometimes vanished. Pinning the row's items to the start keeps it
    // put whatever the text does.
    //
    // A selector can only name the levels it knows about, and this one reaches
    // the cloned row and its children. That was not enough: the container that
    // does the centring sits deeper than that, and how deep is WhatsApp's
    // business and changes with its markup. So this rule covers the row itself,
    // and the clone walk in show() carries the same anchoring the whole way down
    // by asking each element what it actually computes to.
    "#whatly-chatlist-tip>*{align-items:flex-start!important}";

// __CSS__ empty removes the stylesheet, so the same script both collapses and
// restores. The scripting below does three things the CSS cannot: it finds the
// pane columns by measuring them, it reads each filter pill's own first letter,
// and it draws the hover preview.
//
// The preview is a CLONE of the row rather than text scraped out of it. That is
// what makes it look right: the name, the emoji sprites, the unread badge and
// the timestamp all render exactly as they do in the uncollapsed list, with
// nothing to reproduce and nothing to keep in step with WhatsApp's markup. It
// replaces an earlier `title` attribute, which the browser showed only after
// about a second, only while the window had focus, and laid out as a single
// line as wide as the screen.
//
// It hangs off ONE delegated pointerover listener, not a pass over the rows:
// WhatsApp recycles rows as you scroll, so anything that walked the list would
// have to keep re-walking it — the trap the rail-button code warns about at
// length. It runs only while the pointer is already resting on a row.
static const char kScriptTemplate[] = R"JS(
(function () {
  'use strict';
  var TIP = 'whatly-chatlist-tip';
  try {
    // The LIVE desired state, replaced by every re-run of this script. Anything
    // that outlives one run — the timer and the preview below — must read it
    // from here rather than close over it. See the note on sync().
    window.__whatlyStripCss = __CSS__;
    window.__whatlyStripZoom = __ZOOM__;
    var el = document.getElementById('whatly-chatlist-strip');

    // Read each visible row's unread count into an attribute, which the
    // stylesheet above draws as a corner pill.
    //
    // Cheap enough to run on the same one-second tick as everything else,
    // because WhatsApp virtualises the list: #pane-side holds only the couple of
    // dozen rows actually rendered, never the whole chat list. Re-running is also
    // what makes it correct across row recycling — a row reused for a different
    // chat gets its attribute rewritten on the next tick rather than keeping the
    // previous chat's number.
    //
    // The count is found the way chatnav.cpp already finds it: a leaf span whose
    // entire text is the number. Timestamps contain ":" and so are excluded, and
    // it needs no knowledge of WhatsApp's class names or any localised string.
    var markUnread = function () {
      var rows = document.querySelectorAll('#pane-side [role="row"]');
      for (var i = 0; i < rows.length; i++) {
        var spans = rows[i].querySelectorAll('span');
        var count = '', source = null;
        for (var j = 0; j < spans.length; j++) {
          var t = (spans[j].textContent || '').trim();
          if (spans[j].children.length === 0 && /^\d{1,4}$/.test(t)) {
            count = t;
            source = spans[j];
          }
        }
        if (count) {
          rows[i].setAttribute('data-whatly-unread', count);
          // Take the badge's own colours from WhatsApp's, once, so ours matches
          // whatever the theme paints instead of being a fixed green that is only
          // right in one of them. The digits sit in a leaf span, but the fill is
          // usually on an ancestor, so walk up until a real colour appears.
          if (!window.__whatlyUnreadPainted) adoptBadgeColours(source);
        } else {
          rows[i].removeAttribute('data-whatly-unread');
        }
      }
    };

    // Read the live badge's background and text colour and publish them as custom
    // properties for the stylesheet above. Runs once per page: WhatsApp does not
    // change these while it is open, and a theme switch reloads the page.
    var adoptBadgeColours = function (el) {
      try {
        for (var n = el, up = 0; n && up < 4; n = n.parentElement, up++) {
          var cs = getComputedStyle(n);
          var bg = cs.backgroundColor;
          // Anything transparent means the fill is further up.
          if (!bg || bg === 'transparent' || /rgba\([^)]*,\s*0\s*\)$/.test(bg))
            continue;
          var root = document.documentElement;
          root.style.setProperty('--whatly-unread-bg', bg);
          if (cs.color) root.style.setProperty('--whatly-unread-fg', cs.color);
          window.__whatlyUnreadPainted = true;
          return;
        }
      } catch (e) { /* keep the built-in green */ }
    };

    var untag = function (attr) {
      document.querySelectorAll('[' + attr + ']').forEach(function (e) {
        e.removeAttribute(attr);
      });
    };

    // Find every column the pane occupies and mark it, so the stylesheet has
    // something to select. There is more than one: the wrapper that actually
    // holds #side, and a parallel column inside the legacy `.two` container
    // whose only job is to push the conversation across. Both start at the same
    // x as the wrapper and are narrower than half the app; the containers that
    // also start there are full width, so that one test separates them — and it
    // keeps working after the collapse, when the columns are 97px rather than
    // their natural width.
    var prepare = function () {
      var side = document.querySelector('#side');
      if (!side) return false;
      var wrap = side.parentElement;
      var w = wrap.getBoundingClientRect();
      // Remember the natural width while it is still natural: the preview is
      // laid out at exactly the width the list has when it is not collapsed.
      if (w.width > 120) window.__whatlyPaneWidth = w.width;
      untag('data-whatly-pane');
      document.querySelectorAll('div').forEach(function (e) {
        var r = e.getBoundingClientRect();
        // Same left edge AND same height as the wrapper, both to the pixel.
        // Loose tests catch the pane's own children too — #side starts one
        // pixel in, and the virtual-list container is 38000px tall — and
        // pinning a width onto those is at best pointless.
        if (Math.abs(r.left - w.left) > 0.6) return;
        if (Math.abs(r.height - w.height) > 0.6) return;
        e.setAttribute('data-whatly-pane', '1');
      });
      wrap.setAttribute('data-whatly-pane', '1');

      // Words that a 97px column cannot hold. Chats gets away with almost none,
      // but Channels leads with "Channels", "Stay updated on your favourite
      // topics" and "Find channels to follow below", Communities with its own
      // title, and Calls with "Favourites" and "Recent" — all of which come out
      // as ragged fragments a couple of characters wide.
      //
      // Two treatments, because they are two kinds of text. A heading like
      // "Recent" or "View all" still earns its place if it is small enough to
      // fit, so it is shrunk; a sentence never fits at any size worth reading,
      // so it goes. Length is the test — no word list, so it holds in every
      // interface language.
      //
      // The pane's title bar — a <header> that is #side's SIBLING, which is why
      // an earlier walk rooted at #side left the section name behind as a lone
      // capital letter. It is the one piece of text up here with nothing to say
      // at 97px. Leaves only: a container that merely carries a stray text node
      // is structure, and hiding it takes the controls beside it too.
      untag('data-whatly-hide');
      var head = wrap.querySelector('header');
      if (head) {
        head.querySelectorAll('*').forEach(function (e) {
          if (e.children.length > 0) return;
          if (e.closest('button,[role="button"]')) return; // a control's label
          var t = '';
          for (var i = 0; i < e.childNodes.length; i++)
            if (e.childNodes[i].nodeType === 3) t += e.childNodes[i].textContent;
          if (t.trim()) e.setAttribute('data-whatly-hide', '1');
        });
      }

      // WhatsApp's own notices in the list, tagged so the stylesheet above can
      // clip one like a row and the hover preview can show it whole.
      //
      // Read off the live page: the notice is a <span> carrying
      // data-testid="chat-butterbar", a direct child of #side and the immediate
      // PREVIOUS SIBLING of #pane-side — above the list, not inside it. It holds
      // a bare <svg> in a wrapper carrying no data-icon at all, two sentences,
      // and a <button>Refresh</button> nested below its top level.
      //
      // Matched by that testid, which is a name WhatsApp CHOSE — "butterbar" is
      // its own word for this class of bar — and not one its compiler generated.
      // The class strings on the very same element are generated and change with
      // every deploy, so they are worth nothing to match on. A testid can be
      // renamed too, as `data-icon="send"` was, so the slot itself is kept
      // behind it: the one element immediately above the list, and only that
      // one, so a rename can never promote the filter row or the search box
      // into a notice instead.
      //
      // The element STAYS IN THE PAGE once the update has been applied, empty
      // and zero-height, so its presence alone means nothing. The content tests
      // are what tell a notice from that empty shell — measured with nothing to
      // show, it has no button, no icon and no element children at all — and
      // they are what keeps the strip from drawing a permanent phantom cell
      // above every chat list.
      untag('data-whatly-banner');
      var pane = document.querySelector('#pane-side');
      var note = side.querySelector('[data-testid="chat-butterbar"]');
      if (!note && pane) note = pane.previousElementSibling;
      if (note && !note.querySelector('[role="row"]') &&
          note.querySelector('svg,[data-icon]') &&
          note.querySelector('button,[role="button"],a')) {
        var bt = (note.textContent || '').trim();
        // A word fits at 97px and needs nothing doing; a page of text is a
        // container that happened to pass the tests above. Length is the
        // whole test, so this holds in every interface language.
        if (bt.length >= 12 && bt.length <= 400)
          note.setAttribute('data-whatly-banner', '1');
      }

      // The filter pills: the row that holds both the "all" pill and the "more"
      // button, and a letter for each pill that is actually on screen.
      untag('data-whatly-filters');
      untag('data-whatly-cell');
      var all = document.querySelector('#all-filter');
      var more = document.querySelector('#additional-filters');
      if (all && more) {
        var host = all;
        while (host && !host.contains(more)) host = host.parentElement;
        if (host) {
          host.setAttribute('data-whatly-filters', '1');
          // Which children become buttons is decided by what they CONTAIN, not
          // by how big they are: this runs again every time WhatsApp rebuilds
          // the pane, by which point the stylesheet below has already hidden
          // the ones it is about to re-pick, so a measurement would find
          // nothing and the row would empty itself out. The pills that matter
          // are the ones up to and including the "more" button; anything after
          // it is an overflow entry the list keeps collapsed anyway.
          var cells = [];
          var kids = Array.prototype.slice.call(host.children);
          for (var i = 0; i < kids.length; i++) {
            if (kids[i].querySelector('button')) cells.push(kids[i]);
            if (kids[i].contains(more)) break;
          }
          // The "more" cell is kept whatever else is dropped. The stylesheet
          // hides every child that is NOT tagged, so trimming the list to the
          // first four would take the ▾ off screen entirely on any account that
          // shows a fourth pill — WhatsApp adds "Groups" for some — and there
          // would then be no way to reach the other lists without expanding.
          // So the EARLIER pills give way instead, and ▾ keeps the last cell.
          var moreCell = null;
          for (var j = cells.length - 1; j >= 0; j--)
            if (cells[j].contains(more)) {
              moreCell = cells.splice(j, 1)[0];
              break;
            }
          var shown = cells.slice(0, moreCell ? 3 : 4);
          if (moreCell) shown.push(moreCell);
          shown.forEach(function (c) {
            c.setAttribute('data-whatly-cell', '1');
            var b = c.querySelector('button');
            if (!b) return;
            // textContent, NOT innerText: innerText is layout-aware, and by the
            // time this runs again the stylesheet has already set the pills to
            // font-size 0, so innerText reads empty and every button would end
            // up wearing the fallback glyph.
            var text = (c.textContent || '').trim();
            b.setAttribute('data-whatly-letter',
              c.contains(more) || !text ? '▾'
                                        : text.charAt(0).toUpperCase());
          });
        }
      }
      return true;
    };

)JS"
// MSVC caps a single string literal at 16380 bytes and this script is past it.
// Adjacent literals are concatenated by the compiler, so the split below is a
// build constraint only — it changes nothing about the script.
R"JS(
    // The strip is a CHATS thing. Calls is a call log, Status a story list,
    // Channels and Communities are directories — none of them is a list of
    // chats with a conversation beside it, which is the room the strip buys.
    // Every one of them also lays its pane out differently, and the attempts to
    // make them all fit produced far more trouble than width: a column-
    // direction parent in Calls turned the width rule into a height one, the
    // directories lead with paragraphs that came out as ragged fragments, and a
    // community tile's hover highlight kept its pre-collapse width and covered
    // the conversation. So: applied in Chats, withdrawn everywhere else, and
    // the SETTING is left alone throughout — switch back and it collapses
    // again, exactly as it was.
    //
    // Which section is showing is read from the nav rail's aria-pressed rather
    // than from any label, so it holds in every interface language. The rail is
    // the column left of the pane; its topmost entry is Chats.
    var inChats = function () {
      try {
        var rail = [];
        document.querySelectorAll('button[aria-pressed]').forEach(function (b) {
          var r = b.getBoundingClientRect();
          if (r.width > 0 && r.right <= 62 && r.top < 500)
            rail.push({ b: b, y: r.top });
        });
        if (rail.length) {
          rail.sort(function (a, c) { return a.y - c.y; });
          return rail[0].b.getAttribute('aria-pressed') === 'true';
        }
        // No rail to read (a layout without one): fall back to the chat grid
        // actually being on screen.
        var g = document.querySelector('#pane-side [role="grid"]');
        return !!g && g.getClientRects().length > 0;
      } catch (e) {
        return true; // never leave the strip stuck open on a bad guess
      }
    };

    // A filter can leave Chats with no list at all. Pick Favourites with no
    // favourites, or search for something nothing matches, and WhatsApp swaps
    // the rows for an explanatory panel — a heading and a paragraph telling you
    // how to add one — which at 97px is the same ragged-fragment mess the
    // directory sections were, and there is nothing to gain from collapsing a
    // list that is not there. So the strip stands down until rows come back,
    // and as with leaving Chats the SETTING is left alone: clear the filter and
    // it collapses again, exactly as it was.
    var listed = function () {
      try {
        var pane = document.querySelector('#pane-side');
        return !!(pane && pane.querySelector('[role="row"]'));
      } catch (e) {
        return true;
      }
    };

    var wanted = function () { return inChats() && listed(); };

    // Put the stylesheet in or take it out to match "collapsed AND a chat list
    // to collapse". Called on every toggle and once a second, so leaving Chats
    // expands the pane and coming back collapses it again without anything else
    // running.
    //
    // It reads window.__whatlyStripCss, NOT the CSS captured above, and that is
    // a correctness requirement rather than a style: expanding runs this script
    // again with an empty CSS, but the timer started by the FIRST run is still
    // alive with the old closure — so a captured value had it putting the
    // stylesheet straight back a second later, and the strip could not be
    // opened at all. Same reasoning as the live flags object in webtweaks.cpp.
    var sync = function () {
      var style = document.getElementById('whatly-chatlist-strip');
      var want = window.__whatlyStripCss && wanted();
      if (!want) {
        if (style) style.remove();
        var t = document.getElementById(TIP);
        if (t) t.style.display = 'none';
        untag('data-whatly-pane');
        untag('data-whatly-filters');
        untag('data-whatly-cell');
        untag('data-whatly-hide');
        // The uncollapsed list shows WhatsApp's own badge again, so ours must not
        // linger beside it.
        untag('data-whatly-unread');
        return false;
      }
      prepare();
      // Straight away rather than on the next tick, so collapsing does not show a
      // list with no counts on it for up to a second.
      markUnread();
      // A fresh pane gets a fresh budget of catch-up attempts; see the timer.
      window.__whatlyStripTries = 0;
      if (!style) {
        style = document.createElement('style');
        style.id = 'whatly-chatlist-strip';
        (document.head || document.documentElement).appendChild(style);
      }
      style.textContent = window.__whatlyStripCss;
      return true;
    };
    sync();

    if (window.__whatlyStripReady) return;
    window.__whatlyStripReady = true;

    var collapsed = function () {
      var s = document.getElementById('whatly-chatlist-strip');
      return !!(s && s.textContent);
    };
    // WhatsApp rebuilds this pane whenever the section changes, taking the tags
    // with it. A timer costs one querySelector per tick and puts them back; a
    // MutationObserver over a tree WhatsApp mutates continuously would measure
    // the layout several times a second instead.
    // WhatsApp swaps the pane out when the section changes, taking the tags
    // with it, and nothing tells us it happened. A timer costs a couple of
    // querySelector calls per tick and puts everything back; a MutationObserver
    // over a tree WhatsApp mutates continuously would measure the layout
    // several times a second instead — that cost 40% of a core once already.
    setInterval(function () {
      try {
        if (!window.__whatlyStripCss) return; // expanded: nothing to keep up
        var style = document.getElementById('whatly-chatlist-strip');
        var applied = !!style;
        // Leaving or entering Chats, a filter emptying or refilling the list,
        // or WhatsApp rebuilding the pane under us.
        if (applied !== wanted() ||
            (applied && !document.querySelector('[data-whatly-pane]')))
          sync();
        // On a cold start the script runs before WhatsApp has built the filter
        // row, so this is also how the buttons first get their letters.
        //
        // Bounded, because prepare() measures every div in the document and
        // this runs once a second: if WhatsApp ever renames #additional-filters
        // the tagging can never succeed, and an unbounded retry would then pay
        // for a full-document layout read every second for as long as the app
        // is open. Twenty tries is far more than a cold start needs, and the
        // budget is reset by sync() whenever the pane is rebuilt. The counter
        // lives on window, NOT in this closure — the timer outlives the run
        // that created it, same reasoning as __whatlyStripCss above.
        else if (applied && (window.__whatlyStripTries || 0) < 20 &&
                 document.querySelector('#all-filter') &&
                 !document.querySelector('[data-whatly-filters]')) {
          window.__whatlyStripTries = (window.__whatlyStripTries || 0) + 1;
          prepare();
        }
        // Only while collapsed, and only over the rows WhatsApp has rendered.
        if (applied)
          markUnread();
        // A net under the click handler: the panel can also be opened from the
        // keyboard, and this costs one querySelector on a node that is empty
        // whenever no panel is open.
        clampPanel();
      } catch (e) { /* never break the page */ }
    }, 1000);

)JS"
// MSVC caps a single string literal at 16380 bytes and this half of the script
// has grown past it again. Adjacent literals are concatenated by the compiler,
// so this split is a build constraint only — it changes nothing about the script.
R"JS(
    var panel = function () {
      var t = document.getElementById(TIP);
      if (!t) {
        t = document.createElement('div');
        t.id = TIP;
        // pointer-events:none, so moving towards the preview never counts as
        // leaving the row that opened it.
        t.style.cssText = 'position:fixed;z-index:2147483000;display:none;' +
                          'pointer-events:none;border-radius:10px;' +
                          'box-shadow:0 8px 28px rgba(0,0,0,.4);overflow:hidden';
        document.body.appendChild(t);
      }
      return t;
    };
    var timer = null;
    var hide = function () {
      if (timer) { clearTimeout(timer); timer = null; }
      var t = document.getElementById(TIP);
      if (t) t.style.display = 'none';
    };
    var show = function (row) {
      if (!row.isConnected || !collapsed()) return;
      var side = document.querySelector('#side');
      var pane = document.querySelector('#pane-side');
      if (!side || !pane) return;
      var t = panel();
      // Exactly the width the list has when it is not collapsed, remembered by
      // prepare() before the stylesheet narrowed it. The clone is then laid out
      // at that width divided by the zoom, so it has MORE room than the real
      // row rather than less, and the name cannot collide with the timestamp.
      var w = Math.round(window.__whatlyPaneWidth ||
                         document.documentElement.clientWidth * 0.4);
      // Live, not captured: this function outlives the run that defined it, so a
      // captured size would be the one chosen when the page loaded, for ever.
      var zoom = window.__whatlyStripZoom || 1;
      var clone = row.cloneNode(true);
      clone.removeAttribute('id');
      // A notice is clipped to one nowrapped line in the strip; the preview is
      // the place it gets read, so the clone must not inherit that.
      clone.removeAttribute('data-whatly-banner');
      clone.style.zoom = zoom;
      // A chat row is built to hold exactly one line of preview, so its inner
      // blocks have fixed heights and centre what is in them. Let the message
      // wrap inside that and it grows both ways, pushing itself up over the
      // name — which is what a long message did here. Release those heights and
      // anchor every part of the row to its top, so extra lines can only ever
      // grow downwards, into the space below, and are clipped there.
      //
      // Columns are anchored with justify-content and rows with align-items —
      // they are the same instruction along each axis. The rows are the ones that
      // move the avatar: a row that centres what it holds measures its tallest
      // item, so a message wrapped to six lines drops the picture three lines
      // down, and in a panel two rows tall that puts it outside altogether.
      //
      // Asking each element what it computes to, rather than naming a container,
      // is deliberate: every class here is obfuscated and the nesting is
      // WhatsApp's to change. A stylesheet can only reach the levels it names,
      // and the level that does the centring turned out to be below them.
      //
      // The clone is detached, so getComputedStyle would tell us nothing about
      // it; the original is walked alongside instead. cloneNode preserves order,
      // so the two lists line up element for element.
      var src = row.querySelectorAll('*');
      var dst = clone.querySelectorAll('*');
      for (var i = 0; i < src.length && i < dst.length; i++) {
        var cs = getComputedStyle(src[i]);
        dst[i].style.setProperty('height', 'auto', 'important');
        dst[i].style.setProperty('min-height', '0', 'important');
        if (cs.display.indexOf('flex') === -1) continue;
        if (cs.flexDirection === 'column')
          dst[i].style.setProperty('justify-content', 'flex-start', 'important');
        else if (cs.alignItems === 'center' || cs.alignItems === 'flex-end')
          dst[i].style.setProperty('align-items', 'flex-start', 'important');
      }
      t.textContent = '';
      t.appendChild(clone);
      // Take the list's own background, so the preview follows the light/dark
      // theme without either colour being written down here.
      var bg = getComputedStyle(pane).backgroundColor;
      if (!bg || bg === 'rgba(0, 0, 0, 0)')
        bg = getComputedStyle(document.body).backgroundColor;
      t.style.background = bg;
      t.style.width = w + 'px';
      var r = row.getBoundingClientRect();
      // Two rows tall, always: enough for a message that runs on, and a fixed
      // box means a longer one is clipped at the BOTTOM — the ordinary way to
      // treat text that does not fit, and no worse for it, since the middle of
      // a long message is not what anyone is reading a preview for.
      t.style.height = Math.round(r.height * 2 * zoom) + 'px';
      t.style.display = 'block';
      var h = t.getBoundingClientRect().height;
      t.style.left =
        Math.round(side.parentElement.getBoundingClientRect().right + 6) + 'px';
      t.style.top = Math.round(Math.min(Math.max(8, r.top - 6),
                                        window.innerHeight - h - 8)) + 'px';
    };

    document.addEventListener('pointerover', function (ev) {
      try {
        if (!collapsed()) { hide(); return; }
        // A notice is previewed exactly as a row is — same panel, same clone,
        // same delay — because to the reader it IS the topmost chat.
        var row = ev.target.closest &&
                  (ev.target.closest('#pane-side [role="row"]') ||
                   ev.target.closest('[data-whatly-banner]'));
        if (!row) { hide(); return; }
        if (timer) clearTimeout(timer);
        timer = setTimeout(function () { show(row); }, 100);
      } catch (e) { hide(); }
    }, true);
    document.addEventListener('scroll', hide, true);
    document.addEventListener('pointerdown', hide, true);

    // Collapsed, a notice shows its icon and nothing else — and its button is
    // one of the things clipped away, sitting at the right-hand end of a box cut
    // to 97px. So a click that lands on the icon has to be carried to it, or the
    // notice is something you can see and cannot act on.
    //
    // Which control: the first one carrying visible text. A notice that also
    // offers a bare icon button — a dismiss cross is the obvious one — would
    // otherwise have that picked simply for being first in the markup, and
    // dismissing an update prompt is the opposite of what the click meant. Falls
    // back to the first control of any kind, so a notice whose only button is an
    // icon still works.
    //
    // A click that already landed on a control is left entirely alone, so
    // WhatsApp never receives it twice.
    document.addEventListener('click', function (ev) {
      try {
        if (!collapsed()) return;
        if (!ev.target.closest) return;
        var note = ev.target.closest('[data-whatly-banner]');
        if (!note) return;
        if (ev.target.closest('button,[role="button"],a')) return;
        var acts = note.querySelectorAll('button,[role="button"],a');
        var act = null;
        for (var ai = 0; ai < acts.length; ai++) {
          if ((acts[ai].textContent || '').trim()) { act = acts[ai]; break; }
        }
        if (!act) act = acts[0];
        if (!act) return;
        ev.preventDefault();
        // Immediate, because the notice turns out to live in the strip ABOVE
        // #pane-side, and the handler further down treats a click up there as
        // "I want to search" — it opens the list and focuses the box. Both are
        // capture-phase listeners on document, where plain stopPropagation()
        // does not stop the other one, so a click here would otherwise refresh
        // AND uncollapse. Note this is reached only once a control has been
        // found: where there is none, the return above leaves the event alone
        // and the list simply opens, which is the right thing for a notice
        // whose button cannot be carried a click.
        ev.stopImmediatePropagation();
        hide();
        act.click();
      } catch (e) { /* never break the page */ }
    }, true);

    // WhatsApp centres the emoji / GIF / sticker panel on the button that opens
    // it and clamps it against the RIGHT edge of the window only. With the list
    // at full width the composer never sits far enough left for that to matter;
    // collapsed, it moves across by the width the list gave up and the panel is
    // placed at a negative left — measured at -41.5px for a 560px panel — so it
    // is cropped down its own left side. Nudge it back on screen.
    //
    // Only while collapsed, and only when it is actually off screen, so a page
    // WhatsApp has positioned properly is never touched. The panel animates in
    // under a scale transform, and a transformed box measures smaller than the
    // one being positioned, so this waits for that to finish rather than
    // correcting against a half-open size.
    var clampPanel = function () {
      try {
        if (!collapsed()) return;
        var mount = document.getElementById('expressions-panel-container');
        var panel = mount && mount.querySelector('[role="application"]');
        if (!panel) return;
        var cs = getComputedStyle(panel);
        if (cs.transform !== 'none' && cs.transform !== 'matrix(1, 0, 0, 1, 0, 0)')
          return;                                   // still animating open
        var r = panel.getBoundingClientRect();
        if (r.width < 100) return;                  // not open
        var side = document.querySelector('#side');
        var stripRight = side
            ? side.parentElement.getBoundingClientRect().right : 4;
        var vw = document.documentElement.clientWidth;
        // Sit against the conversation's left edge where there is room for the
        // whole panel there, and simply on screen where there is not. Never
        // moved leftwards: a panel WhatsApp has already placed sensibly is left
        // exactly where it put it.
        var want = Math.max(4, Math.min(stripRight, vw - r.width - 4));
        if (r.left >= want - 0.5) return;
        var left = parseFloat(cs.left);
        if (isNaN(left)) return;
        panel.style.setProperty('left', (left + (want - r.left)) + 'px',
                                'important');
      } catch (e) { /* never break the page */ }
    };
    // It opens on a click, and the animation is short; the later pass is what
    // usually does the work, the earlier one just shortens the visible glitch.
    document.addEventListener('click', function () {
      setTimeout(clampPanel, 80);
      setTimeout(clampPanel, 320);
    }, true);
    // Resizing re-runs WhatsApp's own positioning, so it can go off again.
    window.addEventListener('resize', function () { setTimeout(clampPanel, 200); });

    // Collapsed, the search box above the list is clipped to a sliver, so a
    // click up there can only mean "I want to search". Open the list back up
    // and put the cursor in the box, rather than making it two separate
    // actions. Everything inside #side but above #pane-side is that area — no
    // class names — except the filter buttons, which are right there and are
    // meant to work collapsed.
    document.addEventListener('click', function (ev) {
      try {
        if (!collapsed()) return;
        var side = document.querySelector('#side');
        var pane = document.querySelector('#pane-side');
        if (!side || !pane) return;
        if (!side.contains(ev.target) || pane.contains(ev.target)) return;
        var filters = document.querySelector('[data-whatly-filters]');
        if (filters && filters.contains(ev.target)) return;
        // A control inside the notice, which lives up here too. The handler
        // above carries a click on the collapsed notice to its button by
        // calling click() on it, and that arrives as a FRESH event with the
        // button as its target — so without this, this handler cancels the very
        // click that was being delivered, in the capture phase, before the
        // button ever sees it, and opens the search box instead. It is the same
        // for a click the user lands on the button directly.
        //
        // Deliberately only the controls: a click on the notice that reaches no
        // control at all still falls through and opens the list, which is the
        // right outcome for a notice whose button cannot be carried a click.
        if (ev.target.closest &&
            ev.target.closest('[data-whatly-banner] button,' +
                              '[data-whatly-banner] [role="button"],' +
                              '[data-whatly-banner] a')) return;
        ev.preventDefault();
        ev.stopPropagation();
        hide();
        if (window.__whatlyBridge && window.__whatlyBridge.toggleChatListStrip)
          window.__whatlyBridge.toggleChatListStrip();
        // Expanding is a round trip through the app, so the box only exists to
        // be focused a moment later.
        setTimeout(function () {
          var s = document.querySelector(
            '#side [contenteditable="true"],#side [role="textbox"],#side input');
          if (s) s.focus();
        }, 300);
      } catch (e) { /* never break the page */ }
    }, true);
  } catch (e) { /* never break the page */ }
})();
)JS";

namespace {

QString jsStringLiteral(const QString &value) {
  QString escaped = value;
  escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
  return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// Which step a fresh installation starts on. See the note on the zooms above.
QString defaultPreviewSizeId() { return QStringLiteral("medium"); }

} // namespace

namespace ChatListStrip {

bool isCollapsed() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kSettingsKey), false)
      .toBool();
}

void setCollapsed(bool collapsed) {
  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  collapsed);
}

QList<PreviewSize> previewSizes() {
  return {
      {QStringLiteral("small"),
       QCoreApplication::translate("ChatListStrip", "Small")},
      {QStringLiteral("medium"),
       QCoreApplication::translate("ChatListStrip", "Medium")},
      {QStringLiteral("large"),
       QCoreApplication::translate("ChatListStrip", "Large")},
  };
}

QString currentPreviewSizeId() {
  const QString id = SettingsManager::instance()
                         .settings()
                         .value(QLatin1String(kPreviewSizeKey),
                                defaultPreviewSizeId())
                         .toString();
  for (const PreviewSize &size : previewSizes())
    if (size.id == id)
      return id;
  return defaultPreviewSizeId();
}

void setCurrentPreviewSizeId(const QString &id) {
  SettingsManager::instance().settings().setValue(
      QLatin1String(kPreviewSizeKey), id);
}

QString scriptSource() {
  const QString css =
      isCollapsed()
          ? QString::fromLatin1(kCollapseCss).arg(QLatin1String(kStripWidth))
          : QString();
  const QString sizeId = currentPreviewSizeId();
  const char *zoom = sizeId == QLatin1String("large")    ? kPreviewZoomLarge
                     : sizeId == QLatin1String("medium") ? kPreviewZoomMedium
                                                         : kPreviewZoomSmall;
  // fromUtf8, not fromLatin1: this file is UTF-8, so the fallback glyph in the
  // filter grid would otherwise reach the page as one mangled byte per
  // character.
  QString source = QString::fromUtf8(kScriptTemplate);
  source.replace(QLatin1String("__CSS__"), jsStringLiteral(css));
  source.replace(QLatin1String("__ZOOM__"), QLatin1String(zoom));
  return source;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!isCollapsed())
    return;

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace ChatListStrip
