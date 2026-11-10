// Proves which pane the collapsed strip in src/chatliststrip.cpp measures on
// the way back to Chats from another section, and that a mark left on the
// wrong thing is put right. The script is read out of the C++ source and run
// as the page would run it, with the strip's one-second timer driven by hand.
//
// The stand-in page is the app's own row as it was read off the live page at
// 818x802, at the moment this went wrong: the rail, the two layers laid over
// the list, the list's column and the conversation. Calls builds a pane of its
// own INSIDE the second layer, and that pane carries a second #side, ten
// levels further down than the list's and earlier in the document. Both are in
// the page while Calls is open, and for about a tenth of a second after Chats
// is pressed again.
//
// jsdom does no layout, so every box the script measures is given to it in a
// data-box attribute, "left,top,width,height". The boxes of the row's own
// children, of both #side and of the two containers around the Calls pane's
// are the measured ones. The outermost container of the Calls pane is given
// the layer's box, which is an assumption: it is there so that a mark can
// leave the page together with the pane.
//
//   node strip-section-harness.js ../src/chatliststrip.cpp
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

const ROW = '<div role="row" data-box="65,183,367.1,76"><img><span>Pintura</span><span>Sonia: ola</span></div>';
const PAGE = `<!doctype html><html><head></head><body><div id="app">
<div id="row" data-box="0,0,818,802">
  <header data-box="0,0,64,802">
    <button id="chats" aria-pressed="true" data-box="12,10,40,40"></button>
    <button id="calls" aria-pressed="false" data-box="12,60,40,40"></button>
  </header>
  <div id="layer" data-box="64,0,754,802"><span></span></div>
  <div id="over" data-box="0,0,818,802">
    <div id="drawer" data-box="64,0,368.1,802"><div><span id="slot"></span></div></div>
    <div data-box="432.1,0,385.9,802"><span></span></div>
  </div>
  <div id="wrap" data-box="64,0,368.1,802">
    <header data-box="65,0,367.1,64"><span>WhatsApp</span></header>
    <div id="side" data-box="65,64,367.1,738">
      <div id="search"><span data-icon="search"></span><input placeholder="Search or start a new chat"></div>
      <div id="filters" role="tablist"><button id="all-filter" role="tab">All</button><button role="tab">Unread</button><button id="additional-filters"><svg><title>ic-arrow-drop-down</title></svg></button></div>
      <div id="pane-side" data-box="65,176,367.1,626"><div id="grid" role="grid">${ROW}</div></div>
    </div>
  </div>
  <div id="chat" data-box="432.1,0,385.9,802"></div>
</div></div></body></html>`;

// The Calls pane, nested the way the live one is under the layer's own span.
const CALLS = '<div id="calls-pane" data-box="64,0,368.1,802"><span><div><div><div>'
  + '<div><span>Calls</span></div>'
  + '<div id="calls-outer" data-box="65,64,367.1,738"><div id="calls-wrap" data-box="65,64,367.1,738">'
  + '<div id="side" data-box="65,64,367.1,738"><div id="pane-side" data-box="65,128,367.1,674">'
  + '<div role="grid"><div role="row" data-box="65,128,367.1,72"><img><span>Sonia</span></div></div>'
  + '</div></div></div></div></div></div></div></span></div>';

const dom = new JSDOM(PAGE, { runScripts: 'outside-only', pretendToBeVisual: true,
                              url: 'https://web.whatsapp.com/' });
const { window } = dom;
const doc = window.document;
window.Element.prototype.getBoundingClientRect = function () {
  const b = (this.getAttribute('data-box') || '0,0,0,0').split(',').map(Number);
  return { left: b[0], top: b[1], width: b[2], height: b[3], x: b[0], y: b[1],
           right: b[0] + b[2], bottom: b[1] + b[3] };
};
const ticks = [];
window.setInterval = (fn) => { ticks.push(fn); return ticks.length; };
// Every run of prepare() writes this attribute at least once, so counting the
// writes counts the times the whole document was measured.
let measured = 0;
const setAttribute = window.Element.prototype.setAttribute;
window.Element.prototype.setAttribute = function (name, value) {
  if (name === 'data-whatly-pane') measured++;
  return setAttribute.call(this, name, value);
};

const $ = (sel) => doc.querySelector(sel);
const tick = (n = 1) => { for (let i = 0; i < n; i++) ticks.forEach((f) => f()); };
const marked = (sel) => $(sel).hasAttribute('data-whatly-pane');
const marks = () => doc.querySelectorAll('[data-whatly-pane]').length;
const collapsed = () => !!doc.getElementById('whatly-chatlist-strip');
// The list's own #side and the Calls pane's share the id, so they are told
// apart here by where they sit.
const LIST = '#wrap > #side';
const press = (id) => ['#chats', '#calls'].forEach(
  (s) => $(s).setAttribute('aria-pressed', String(s === id)));
const openCalls = () => { press('#calls'); $('#slot').innerHTML = CALLS; };
const closeCalls = () => { $('#slot').innerHTML = ''; };

let ok = 0, bad = 0;
const check = (name, cond, more) => {
  if (cond) { ok++; console.log('  PASS  ' + name); }
  else { bad++; console.log('  FAIL  ' + name + (more ? '  -> ' + more : '')); }
};
const columns = (what) => {
  check(what + ': the list\'s column is marked', marked('#wrap'));
  check(what + ': so are the two layers laid over it', marked('#layer') && marked('#drawer'));
  check(what + ': the list\'s own #side is not', !marked(LIST));
};

window.eval(script);
check('the strip ran and started its timer', ticks.length === 1, String(ticks.length));

console.log('\nCollapsed in Chats');
columns('at rest');
check('three marks and no more', marks() === 3, String(marks()));

console.log('\nCalls');
openCalls();
tick();
check('the stylesheet is out and no mark is left', !collapsed() && marks() === 0, String(marks()));

console.log('\nBack to Chats, the timer falling while the Calls pane is still in the page');
press('#chats');
tick();
check('collapsed again on that pass', collapsed());
columns('with both panes in the page');
check('nothing inside the Calls pane was taken for the column',
      !marked('#calls-outer') && !marked('#calls-wrap') && !marked('#calls-wrap > #side'));
closeCalls();
measured = 0;
tick(5);
columns('once the Calls pane has gone');
check('three marks and no more', marks() === 3, String(marks()));
check('and the page was not measured again over five quiet passes', measured === 0, String(measured));

console.log('\nBack to Chats, the timer falling after the Calls pane has gone');
openCalls();
tick();
press('#chats');
closeCalls();
tick();
columns('with one pane in the page');

console.log('\nA mark that outlives the column it was for');
$('#wrap').removeAttribute('data-whatly-pane');
$(LIST).setAttribute('data-whatly-pane', '1');
tick();
columns('one pass later');
measured = 0;
tick(5);
check('and the page was not measured again over five quiet passes', measured === 0, String(measured));

console.log('\nA filter that leaves Chats with no rows, behind a Calls pane that has some');
openCalls();
tick();
$('#grid').innerHTML = '';
press('#chats');
tick();
check('the strip stands down, as it does for an empty list', !collapsed() && marks() === 0, String(marks()));
closeCalls();
tick();
check('and still does once the Calls pane has gone', !collapsed());
$('#grid').innerHTML = ROW;
tick();
check('rows back, it collapses again', collapsed());
columns('with the rows back');

console.log('\n' + ok + ' passed, ' + bad + ' failed');
process.exit(bad ? 1 : 0);
