// Proves the scroll-smoothness probe before it is trusted with a measurement.
// Run: node scroll-harness.js   (from probes/)
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, 'scroll-smoothness-probe.js');
const code = fs.readFileSync(SRC, 'utf8');

let pass = 0, fail = 0;
const ok = (name, cond, extra) => {
  if (cond) { pass++; console.log(`  ok   ${name}`); }
  else { fail++; console.log(`  FAIL ${name}${extra ? '  -> ' + extra : ''}`); }
};

const dom = new JSDOM('<!doctype html><body></body>', {
  url: 'https://web.whatsapp.com/',
  runScripts: 'outside-only',
  pretendToBeVisual: true,
});
const { window } = dom;

// Watch how the probe registers its listeners: a probe that blocks the
// compositor would be measuring its own cost.
const seen = [];
for (const t of [window, window.document]) {
  const orig = t.addEventListener.bind(t);
  t.addEventListener = (type, fn, opts) => { seen.push({ type, opts }); return orig(type, fn, opts); };
}

const KEY = '__whatly_scroll_probe';
const read = () => JSON.parse(window.localStorage.getItem(KEY) || 'null');

const wheel = (dy, ctrl = false) =>
  window.dispatchEvent(new window.WheelEvent('wheel', { deltaY: dy, ctrlKey: ctrl, bubbles: true }));

const pane = window.document.createElement('div');
window.document.body.appendChild(pane);
let top = 0;
Object.defineProperty(pane, 'scrollTop', { get: () => top, configurable: true });
const scrollTo = (v) => { top = v; pane.dispatchEvent(new window.Event('scroll')); };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  window.eval(code);

  console.log('probe shape');
  ok('every listener is passive', seen.length > 0 && seen.every((s) => s.opts && s.opts.passive === true),
     JSON.stringify(seen));
  ok('listens for wheel and scroll',
     seen.some((s) => s.type === 'wheel') && seen.some((s) => s.type === 'scroll'));
  ok('writes an armed timestamp at install', !!(read() || {}).armed);
  ok('starts with no bursts', Array.isArray(read().bursts) && read().bursts.length === 0);

  console.log('a short flick is not a measurement');
  wheel(50); wheel(50); scrollTo(40);
  await sleep(1100);
  ok('fewer than five wheels records nothing', read().bursts.length === 0);

  console.log('a real burst, scrolling down cleanly');
  for (let i = 0; i < 6; i++) { wheel(100); scrollTo(100 + i * 100); }
  await sleep(1100);
  let b = read().bursts;
  ok('one burst recorded', b.length === 1, `got ${b.length}`);
  ok('wheels counted', b[0] && b[0].wheels === 6, b[0] && String(b[0].wheels));
  ok('latency measured', b[0] && typeof b[0].latAvg === 'number' && b[0].latAvg >= 0);
  ok('no backwards motion on a clean run', b[0] && b[0].backwards === 0);

  console.log('content sliding back while the wheel kept going down');
  for (let i = 0; i < 5; i++) wheel(100);
  scrollTo(1000); scrollTo(1100); scrollTo(1040);   // 60px back, wheel still down
  await sleep(1100);
  b = read().bursts;
  ok('backwards motion caught', b[1] && b[1].backwards === 1, b[1] && String(b[1].backwards));
  ok('distance recorded', b[1] && b[1].backMaxPx === 60, b[1] && String(b[1].backMaxPx));

  console.log('an honest change of direction is not a glitch');
  for (let i = 0; i < 5; i++) wheel(100);
  scrollTo(2000); scrollTo(2100);
  for (let i = 0; i < 3; i++) wheel(-100);          // user now scrolls up
  scrollTo(1900);
  await sleep(1100);
  b = read().bursts;
  ok('scrolling up counts no backwards', b[2] && b[2].backwards === 0, b[2] && String(b[2].backwards));

  console.log('zoom is not scrolling');
  for (let i = 0; i < 6; i++) wheel(100, true);
  await sleep(1100);
  ok('ctrl+wheel starts no burst', read().bursts.length === 3, String(read().bursts.length));

  console.log('the log stays bounded');
  for (let n = 0; n < 8; n++) {
    for (let i = 0; i < 5; i++) { wheel(100); scrollTo(3000 + n * 500 + i * 50); }
    await sleep(1000);
  }
  ok('kept to the last eight bursts', read().bursts.length === 8, String(read().bursts.length));

  console.log(`\n${pass}/${pass + fail} passed`);
  process.exit(fail ? 1 : 0);
})();
