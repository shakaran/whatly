// Proves the probe captures the notice and NOT the filter tablist. The tablist
// here is the real one off Gert's page: role="tablist",
// aria-label="chat-list-filters", an svg whose only name is its <title>.
const fs = require('fs');
const { JSDOM } = require('jsdom');
const PROBE = fs.readFileSync(process.argv[2], 'utf8');

const HTML = `<!doctype html><html><body><div id="app">
  <div id="side">
    <header><span>WhatsApp</span></header>
    <div class="searchbox"><span data-icon="search"></span><input placeholder="Search or start a new chat"></div>
    <div class="filters" role="tablist" aria-label="chat-list-filters" tabindex="-1">
      <button id="all-filter" role="tab" aria-selected="true">All</button>
      <button role="tab">Unread4</button>
      <button role="tab">Favourites</button>
      <button role="tab">Groups2</button>
      <button id="additional-filters"><svg><title>ic-arrow-drop-down</title></svg></button>
    </div>
    <div id="pane-side">
      <div class="notice">
        <span data-icon="refresh"><svg><title>refresh</title></svg></span>
        <div><h2>Refresh to update</h2><div>A new version of WhatsApp is available.</div>
        <a id="refresh" role="button">Refresh</a></div>
      </div>
      <div role="grid"><div role="row"><img><span>Pintura</span><span>Sonia: ola</span></div></div>
    </div>
  </div></div></body></html>`;

const dom = new JSDOM(HTML, { runScripts: 'outside-only', pretendToBeVisual: true, url: 'https://web.whatsapp.com/' });
const { window } = dom; const doc = window.document;
const warns = []; window.console.warn = (m) => warns.push(String(m));

window.eval(PROBE);

setTimeout(() => {
  let ok = 0, bad = 0;
  const check = (n, c, x) => { if (c) { ok++; console.log('  PASS  ' + n); } else { bad++; console.log('  FAIL  ' + n + (x ? '  -> ' + x : '')); } };
  const raw = window.localStorage.getItem('__whatly_banner_probe');
  check('the probe captured something', !!raw);
  let arr = []; try { arr = JSON.parse(raw || '[]'); } catch (e) {}
  check('it stores a list', Array.isArray(arr), typeof arr);
  const texts = arr.map(a => a.text);
  check('it captured the NOTICE', texts.some(t => t.includes('A new version of WhatsApp is available')), JSON.stringify(texts));
  check('it did NOT capture the filter tablist', !texts.some(t => t.includes('AllUnread')), JSON.stringify(texts));
  check('it did NOT capture the search box', !texts.some(t => (t || '').includes('Search or start')), JSON.stringify(texts));
  check('the capture is marked as coming from the list', arr.some(a => a.where === 'pane-side'), JSON.stringify(arr.map(a => a.where)));
  check('it recorded the clickable', arr.some(a => (a.clickables || []).some(c => c.text === 'Refresh')));
  console.log('\n' + ok + ' passed, ' + bad + ' failed');
  process.exit(bad ? 1 : 0);
}, 400);
