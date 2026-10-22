// Proves the collapsed strip's notice handling in src/chatliststrip.cpp against
// a stand-in page: which bar is tagged, when a tag is kept and when it has to
// go, and where a click on a collapsed notice is carried. The script is read
// out of the C++ source and run as the page would run it, with the strip's
// one-second timer driven by hand.
//
// jsdom does no layout, so every box the script measures is given to it in a
// data-box attribute, "left,top,width,height"; an element without one measures
// as nothing, which is also how an element taken out of the layout measures.
//
//   node strip-notice-harness.js ../src/chatliststrip.cpp
const fs = require('fs');
const { JSDOM } = require('jsdom');

const src = fs.readFileSync(process.argv[2], 'utf8');
const from = src.indexOf('kScriptTemplate[] =');
const upto = src.indexOf(')JS";', from) + 4;
let script = '';
for (const m of src.slice(from, upto).matchAll(/R"JS\(([\s\S]*?)\)JS"/g))
  script += m[1];
script = script.split('__CSS__').join(JSON.stringify('[data-whatly-pane]{}'))
               .split('__ZOOM__').join('0.85')
               .split('__DISMISS__').join(JSON.stringify('Dismiss'));

const PAGE = `<!doctype html><html><head></head><body>
<div id="rail"><button aria-pressed="true" data-box="0,60,48,48">Chats</button></div>
<div id="wrap" data-box="62,0,97,760"><div id="side" data-box="63,0,96,760">
  <header><span>WhatsApp</span></header>
  <div id="search"><span data-icon="search"></span><input placeholder="Search or start a new chat"></div>
  <div id="filters" role="tablist"><button id="all-filter" role="tab">All</button><button role="tab">Unread</button><button id="additional-filters"><svg><title>ic-arrow-drop-down</title></svg></button></div>
  <span data-testid="chat-butterbar" id="bar" data-box="63,120,96,200"></span>
  <div id="pane-side" data-box="63,330,96,260">
    <div role="grid"><div role="row" data-box="63,330,96,72"><img><span>Pintura</span><span>Sonia: ola</span></div></div>
  </div>
  <div id="below"></div>
</div></div></body></html>`;

// The update notice: a bare svg in a wrapper with no data-icon, two sentences,
// and a Refresh button nested below the top level. The cross comes last.
const UPDATE = '<div id="note"><div id="pic"><svg><title>ic-sync</title></svg></div>'
  + '<div>Refresh to update<div>A new version of WhatsApp is available.</div>'
  + '<button id="refresh">Refresh</button></div>'
  + '<div id="xbox"><button id="cross"><svg><title>ic-close</title></svg></button></div></div>';
// A notice whose action is a bare icon, with the cross first in the markup.
const ICON_ACTION = '<div id="note2"><div id="xbox2"><button id="cross2"><svg><title>ic-close</title></svg></button></div>'
  + '<div id="pic2"><svg><title>ic-laptop</title></svg></div>'
  + '<div>Get the desktop app for faster calls.<a id="get" role="button"><svg><title>ic-arrow</title></svg></a></div></div>';
// A notice that offers nothing but the cross.
const CROSS_ONLY = '<div id="note3"><div id="pic3"><svg><title>ic-info</title></svg></div>'
  + '<div>Messages are syncing with your phone.</div>'
  + '<div><button id="cross3"><svg><title>ic-close</title></svg></button></div></div>';
// A promotion below the list: no name, no role, no control at rest.
const PROMO = '<div id="promo" data-box="63,600,96,150"><div id="ppic"><img></div>'
  + '<div id="ptext" data-box="80,600,30,150">Download WhatsApp for Linux and keep chatting</div></div>';

const dom = new JSDOM(PAGE, { runScripts: 'outside-only', pretendToBeVisual: true,
                              url: 'https://web.whatsapp.com/' });
const { window } = dom;
const doc = window.document;
window.Element.prototype.getBoundingClientRect = function () {
  const b = (this.getAttribute('data-box') || '0,0,0,0').split(',').map(Number);
  return { left: b[0], top: b[1], width: b[2], height: b[3], x: b[0], y: b[1],
           right: b[0] + b[2], bottom: b[1] + b[3] };
};
// Visibility is the box alone here, whatever this jsdom does or does not know.
window.Element.prototype.checkVisibility = function () { return true; };
const ticks = [];
window.setInterval = (fn) => { ticks.push(fn); return ticks.length; };
let toggles = 0;
window.__whatlyBridge = { toggleChatListStrip: () => { toggles++; } };

const $ = (sel) => doc.querySelector(sel);
const tick = (n = 1) => { for (let i = 0; i < n; i++) ticks.forEach((f) => f()); };
const tagged = (sel) => !!$(sel) && $(sel).hasAttribute('data-whatly-banner');
const marks = (sel) => $(sel).querySelectorAll(
  '[data-whatly-icon],[data-whatly-icon-path],[data-whatly-dismiss]').length;
const box = (sel, b) => { if (b) $(sel).setAttribute('data-box', b); else $(sel).removeAttribute('data-box'); };
const clicked = [];
const watchClicks = () => ['#refresh', '#cross', '#get', '#cross2', '#cross3'].forEach((s) => {
  const e = $(s);
  if (e && !e.__watched) { e.__watched = true; e.addEventListener('click', () => clicked.push(s)); }
});
const clickOn = (sel) => {
  clicked.length = 0;
  toggles = 0;
  watchClicks();
  $(sel).dispatchEvent(new window.MouseEvent('click', { bubbles: true, cancelable: true }));
};

let ok = 0, bad = 0;
const check = (name, cond, more) => {
  if (cond) { ok++; console.log('  PASS  ' + name); }
  else { bad++; console.log('  FAIL  ' + name + (more ? '  -> ' + more : '')); }
};

$('#bar').innerHTML = UPDATE;
window.eval(script);
check('the strip ran and started its timer', ticks.length === 1, String(ticks.length));

console.log('\nThe update notice in the butterbar');
check('it is tagged', tagged('#bar'));
check('its picture is named, not the cross', $('#pic').hasAttribute('data-whatly-icon'));
check('the cross is named as the dismiss control', $('#xbox').hasAttribute('data-whatly-dismiss'));
check('the cross is labelled in the interface language', $('#cross').getAttribute('title') === 'Dismiss');
box('#bar', '63,120,96,48');
tick(3);
check('drawn as a 48px icon, it keeps its tag over three passes', tagged('#bar'));
clickOn('#pic svg');
check('a click on its picture runs Refresh', clicked.join() === '#refresh', clicked.join());

console.log('\nThe same bar, refilled by WhatsApp with something that is not a notice');
$('#bar').innerHTML = '<div><span data-icon="search"></span><input placeholder="Search archived chats"></div>';
tick();
check('a bar refilled with a search box loses its tag', !tagged('#bar'));
check('and keeps none of the names inside it', marks('#bar') === 0, String(marks('#bar')));
$('#bar').innerHTML = UPDATE;
tick();
check('a fresh notice in the same bar is tagged again', tagged('#bar'));
$('#bar').innerHTML = '<div><span data-icon="archived"></span><div>' + 'Archived chats stay archived. '.repeat(16) + '</div></div>';
tick();
check('a bar refilled with a page of text loses its tag', !tagged('#bar'));
$('#bar').innerHTML = UPDATE;
tick();
$('#bar').innerHTML = '<div role="tablist"><button id="all-filter" role="tab">All</button>'
  + '<button role="tab">Unread</button>'
  + '<button id="additional-filters"><svg><title>ic-arrow-drop-down</title></svg></button></div>';
tick();
check('a bar refilled with the filter row loses its tag', !tagged('#bar'));
$('#bar').innerHTML = '';
tick();
check('the spent, empty shell carries no tag', !tagged('#bar'));

console.log('\nWhere a click on a collapsed notice goes');
$('#bar').innerHTML = ICON_ACTION;
tick();
check('a notice whose action is a bare icon is tagged', tagged('#bar'));
check('its cross is the one named as the dismiss control', $('#xbox2').hasAttribute('data-whatly-dismiss'));
clickOn('#pic2 svg');
check('a click on its picture runs the icon action, not the cross', clicked.join() === '#get', clicked.join() || 'nothing');
$('#bar').innerHTML = CROSS_ONLY;
tick();
clickOn('#pic3 svg');
check('a notice offering only the cross is never dismissed by a click on its picture', clicked.length === 0, clicked.join());
check('that click opens the list instead', toggles === 1, String(toggles));

console.log('\nA promotion below the list, reachable only by its own wrapping');
$('#bar').innerHTML = '';
tick();
$('#below').innerHTML = PROMO;
tick();
check('it is found by its wrapping and tagged', tagged('#promo'));
box('#ptext');
box('#promo', '63,600,96,48');
tick(3);
check('drawn as an icon it no longer wraps, and it keeps its tag', tagged('#promo'));
box('#promo', '63,900,96,48');
tick();
check('scrolled out of sight for a moment, it keeps its tag', tagged('#promo'));
box('#promo', '63,600,96,48');
$('#promo').innerHTML = '<div><span data-icon="search"></span><input placeholder="Search"></div>';
tick();
check('its container refilled with a search box loses its tag', !tagged('#promo'));
check('and keeps none of the names inside it', marks('#promo') === 0, String(marks('#promo')));
$('#promo').outerHTML = PROMO;
tick();
check('a promotion shown there again is tagged again', tagged('#promo'));
$('#promo').innerHTML = '<div id="ppic"><img></div><div>' + 'A long explanation of the new privacy settings. '.repeat(10) + '</div>';
tick();
check('its container refilled with a page of text loses its tag', !tagged('#promo'));

console.log('\n' + ok + ' passed, ' + bad + ' failed');
process.exit(bad ? 1 : 0);
