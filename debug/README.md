# Debug probes

Instrumentation for questions that cannot be answered by reading the code: what a
user is actually seeing in the embedded page, measured rather than guessed.

Nothing here ships with Whatly. These are Custom JS addons and Node harnesses,
used during an investigation and then left behind so the next one does not start
from nothing.

## Why measure at all

During the 2026sep01 scroll investigation, four separate hypotheses about a
reported jerkiness — a blocking wheel listener, an expensive `MutationObserver`,
distance per wheel notch, and Chromium's vsync scheduling — each looked
convincing and each was killed by a measurement. Two of the probes' own metrics
also turned out to be measuring the wrong thing. Impressions, including the
author's, were wrong far more often than numbers were.

## `scroll-smoothness-probe.js`

Turns "scrolling feels bad" into figures. Install as a Custom JS addon
(Settings → Custom JS), restart, scroll a chat, and read
`localStorage.__whatly_scroll_probe`.

Every listener it registers is **passive**. A probe that took scrolling off the
compositor thread would be measuring its own cost.

Per burst of wheeling (a burst ends after 900 ms of quiet; fewer than five
notches is discarded as a stray flick):

| field | meaning |
|---|---|
| `wheels`, `seconds`, `wheelsPerSec` | how hard the wheel was actually turned |
| `pxPerWheel` | distance the content travels per notch — 100 is stock Chromium |
| `dyAvg`, `dyMax` | the `deltaY` the page is handed, before any scrolling happens |
| `latAvg`, `latMax` | wheel tick → pixels actually moving, in ms |
| `visJumpMax`, `visJump200` | biggest single **visible** step, and how many were large |
| `wobble`, `wobbleMaxPx` | the picture reversing direction while the wheel never did |
| `reanchor` | `scrollTop` moved far while the screen stayed still — a history load, not a defect |
| `pctOver33`, `gapOver100`, `gapMax` | frame pacing |
| `g33`…`g200p` | frame-gap histogram, because a run full of 60 ms gaps never trips a 100 ms counter |

Two of those exist because earlier versions were wrong, and the mistakes are
worth knowing:

- **`scrollTop` is not what the eye sees.** When WhatsApp prepends older
  messages it *must* change `scrollTop` to hold the view still, and nothing moves
  on screen. Counting that as a jump made the probe report jumps a user could
  not see. Visible movement is now `Δ scrollTop − Δ scrollHeight`.
- **An element anchor is useless in a virtualised list.** The element under the
  viewport centre is recycled almost every frame during a fast scroll, so an
  anchor-based metric silently returns zero on exactly the long bursts that
  matter. Arithmetic cannot be recycled; elements can.

## `scroll-probe-console.js`

The same probe plus a `__whatlyReport()` that prints a `console.table`. Paste it
into DevTools on `web.whatsapp.com` in an ordinary browser to get a comparison
against stock Chromium. Identical measurement code on purpose — a second
implementation would let a difference in method masquerade as a difference in
browsers.

## `update-banner-probe.js`

Captures the structure of WhatsApp's "Refresh to update" notice when one appears,
so a feature can be built against the real markup instead of a guess. Records up
to four captures as a list; a wrong match can never block the right one.

It deliberately has **no `MutationObserver`**. One was tried, on
`document.documentElement` with `subtree: true`, and it polluted the very frame
timings another probe was reading. Whatly's own strip code refuses the same
pattern and says why in `src/chatliststrip.cpp` — "a MutationObserver over a tree
WhatsApp mutates continuously would measure the layout several times a second
instead". An initial sweep plus a five-second poll costs nothing and misses
nothing that sits on screen for minutes.

## Harnesses

Both probes are proved against a stand-in DOM before being trusted with a
measurement.

```bash
cd debug
npm install
node scroll-harness.js                          # 14 assertions
node banner-probe-harness.js update-banner-probe.js   # 7 assertions
```

`jsdom@24` is pinned: jsdom 25+ pulls an ESM-only dependency that Node 20's
`require` cannot load.

The scroll harness asserts, among other things, that every listener is passive,
that a short flick records nothing, that backwards motion is caught with its
distance, that an honest change of direction is *not* counted as a glitch, and
that Ctrl+wheel starts no burst.
