// Whatly scroll-smoothness probe.
//
// Measures what "not smooth" actually is, so a fix can be judged by numbers
// instead of by impression. Install as a Custom JS addon, restart, scroll a
// busy chat with the wheel for a few seconds, and read the result back off
// disk from the profile's Local Storage.
//
// Every listener here is PASSIVE. A probe that took scrolling off the
// compositor thread would be measuring itself.
(function () {
  'use strict';
  var KEY = '__whatly_scroll_probe';
  var IDLE_MS = 900;          // this much quiet ends a burst
  var BACK_PX = 4;            // ignore sub-pixel/rounding wobble
  var MAX_BURSTS = 8;

  function save(obj) {
    try { localStorage.setItem(KEY, JSON.stringify(obj)); } catch (e) {}
  }
  function load() {
    try { return JSON.parse(localStorage.getItem(KEY)) || null; } catch (e) { return null; }
  }

  var store = load();
  if (!store || !Array.isArray(store.bursts)) store = { bursts: [] };
  store.armed = new Date().toISOString();      // liveness, readable from disk
  store.ua = (navigator.userAgent || '').slice(0, 120);
  store.dpr = window.devicePixelRatio;
  save(store);

  // Watching an element's position ON SCREEN, not scrollTop. When WhatsApp
  // prepends older messages, scrollTop MUST change to keep the view still --
  // nothing moves, and counting that as a jump is simply wrong. Only a change in
  // where an anchor actually sits on screen is something a reader can see.
  // An element anchor is useless here. In a virtualised list the element under
  // the viewport centre is recycled almost every frame during a fast scroll, so
  // isConnected fails, the anchor is re-picked, and no delta is ever computed --
  // which is why this returned a flat zero on the long bursts. Use arithmetic
  // that cannot be recycled instead:
  //
  //     visible movement = change in scrollTop - change in scrollHeight
  //
  // Prepending H pixels of history raises both by H, so the difference is zero,
  // which is right because nothing moved on screen. Ordinary scrolling leaves
  // scrollHeight alone, so the difference is the scroll itself.
  var lastVisSign = 0, lastHeight = null;

  var burst = null, idleTimer = null, rafId = null, lastFrame = 0;
  var pendingWheels = [];     // performance.now() of wheels awaiting a scroll
  var dirRun = 0;             // consecutive wheels in one direction (signed)
  var lastTop = null, lastTarget = null;

  function newBurst() {
    return {
      at: new Date().toISOString(),
      t0: performance.now(),
      wheels: 0, scrolls: 0,
      latSum: 0, latN: 0, latMax: 0,   // wheel -> first scroll response, ms
      backwards: 0, backMaxPx: 0,      // scroll moved against the wheel
      travelPx: 0,                     // total distance the content actually moved
      dySum: 0, dyMax: 0,              // the deltaY the page is actually handed
      visBack: 0, visBackMaxPx: 0,     // content moved on SCREEN against the wheel
      visJumpMax: 0, visJump200: 0,    // biggest single VISIBLE step, and how many were big
      wobble: 0, wobbleMaxPx: 0,       // direction reversals while the wheel held ONE way
      g33: 0, g50: 0, g100: 0, g200: 0, g200p: 0,   // frame-gap histogram
      reanchor: 0,                     // scrollTop moved a lot but nothing moved on screen
      visSamples: 0,
      frames: 0, gapMax: 0, gapOver33: 0, gapOver100: 0
    };
  }

  function tick(ts) {
    if (!burst) { rafId = null; return; }
    if (lastFrame) {
      var gap = ts - lastFrame;
      burst.frames++;
      if (gap > burst.gapMax) burst.gapMax = gap;
      if (gap > 33) burst.gapOver33++;      // missed a 30fps deadline
      if (gap > 100) burst.gapOver100++;    // visibly stalled
      // Where the gaps actually sit. A run full of 60ms gaps reads as constant
      // stutter yet never trips the 100ms counter at all.
      if (gap <= 33) burst.g33++;
      else if (gap <= 50) burst.g50++;
      else if (gap <= 100) burst.g100++;
      else if (gap <= 200) burst.g200++;
      else burst.g200p++;
    }
    lastFrame = ts;
    rafId = requestAnimationFrame(tick);
  }

  function endBurst() {
    idleTimer = null;
    if (rafId) { cancelAnimationFrame(rafId); rafId = null; }
    lastFrame = 0;
    if (!burst) return;
    if (burst.wheels >= 5) {              // ignore stray single clicks
      // Duration and wheel rate, so "does scrolling slower help?" is a question
      // the numbers can answer instead of an impression.
      burst.seconds = +((performance.now() - burst.t0) / 1000).toFixed(1);
      burst.wheelsPerSec = burst.seconds ? +(burst.wheels / burst.seconds).toFixed(1) : null;
      burst.backPer100 = +(100.0 * burst.backwards / burst.wheels).toFixed(1);
      // How far the content moves for one notch of the wheel. This is the number
      // that says whether one app simply scrolls further per tick than another.
      burst.pxPerWheel = +(burst.travelPx / burst.wheels).toFixed(0);
      burst.wobblePerSec = burst.seconds ? +(burst.wobble / burst.seconds).toFixed(1) : null;
      burst.dyAvg = +(burst.dySum / burst.wheels).toFixed(0);
      delete burst.dySum;
      burst.pctOver33 = burst.frames ? +(100.0 * burst.gapOver33 / burst.frames).toFixed(1) : null;
      delete burst.t0;
      burst.latAvg = burst.latN ? +(burst.latSum / burst.latN).toFixed(1) : null;
      burst.latMax = +burst.latMax.toFixed(1);
      burst.gapMax = +burst.gapMax.toFixed(1);
      delete burst.latSum; delete burst.latN;
      var s = load() || { bursts: [] };
      if (!Array.isArray(s.bursts)) s.bursts = [];
      s.bursts.push(burst);
      while (s.bursts.length > MAX_BURSTS) s.bursts.shift();
      s.updated = new Date().toISOString();
      save(s);
    }
    burst = null;
    pendingWheels = [];
    dirRun = 0;
    lastTop = null; lastTarget = null; lastVisSign = 0; lastHeight = null;
  }

  function touch() {
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = setTimeout(endBurst, IDLE_MS);
  }

  window.addEventListener('wheel', function (ev) {
    if (ev.ctrlKey) return;               // zoom gesture, not a scroll
    if (!burst) { burst = newBurst(); rafId = requestAnimationFrame(tick); }
    burst.wheels++;
    // What the page is handed per notch. A stock Chromium mouse notch is 100.
    // Much more than that means the events arriving are already chunky, which is
    // a different bug from the page scrolling too far for a normal delta.
    var ady = Math.abs(ev.deltaY || 0);
    burst.dySum += ady;
    if (ady > burst.dyMax) burst.dyMax = ady;
    pendingWheels.push(performance.now());
    if (pendingWheels.length > 60) pendingWheels.shift();
    var d = ev.deltaY > 0 ? 1 : (ev.deltaY < 0 ? -1 : 0);
    if (d) dirRun = (dirRun > 0 && d > 0) || (dirRun < 0 && d < 0) ? dirRun + d : d;
    touch();
  }, { passive: true, capture: true });

  document.addEventListener('scroll', function (ev) {
    if (!burst) return;
    var el = ev.target;
    var top = (el && el.scrollTop !== undefined) ? el.scrollTop
            : (document.scrollingElement || document.documentElement).scrollTop;
    burst.scrolls++;

    // Latency: how long the oldest unanswered wheel waited for pixels to move.
    if (pendingWheels.length) {
      var lat = performance.now() - pendingWheels[0];
      pendingWheels.length = 0;
      burst.latSum += lat; burst.latN++;
      if (lat > burst.latMax) burst.latMax = lat;
    }

    // Backwards motion: content sliding back while the wheel kept going one
    // way. Only counted after a settled run in that direction, so an honest
    // change of direction by the user is not mistaken for a glitch.
    if (el !== lastTarget) lastHeight = null;
    if (el === lastTarget && lastTop !== null) {
      burst.travelPx += Math.abs(top - lastTop);

      // Compare what scrollTop did with what the reader could actually see.
      var scrollDelta = top - lastTop;
      var visDelta = null;
      var h = el.scrollHeight;
      if (lastHeight !== null) visDelta = scrollDelta - (h - lastHeight);
      lastHeight = h;
      if (visDelta === null) {
        /* first sample for this scroller: nothing to compare against yet */
      } else {
        burst.visSamples++;
        // How far the page visibly moved in ONE step. This is the lurch a
        // reader actually sees when a stall lets go, and nothing else here
        // measures it: travelPx is a total and gapMax is only time.
        var av = Math.abs(visDelta);
        if (av > burst.visJumpMax) burst.visJumpMax = Math.round(av);
        if (av > 200) burst.visJump200++;

        // The picture reversing direction while the wheel never did. A scroll
        // animator that overshoots and springs back does this at a few pixels
        // and a frame or two, far too small for visJump and far too quick to
        // show up as a frame gap -- but it is what a reader calls stutter.
        if (av >= 2 && Math.abs(dirRun) >= 3) {
          var sign = visDelta > 0 ? 1 : -1;
          if (lastVisSign !== 0 && sign !== lastVisSign) {
            burst.wobble++;
            if (av > burst.wobbleMaxPx) burst.wobbleMaxPx = Math.round(av);
          }
          lastVisSign = sign;
        }
        // Large scrollTop move, nothing moved on screen: a history prepend
        // being correctly absorbed. Invisible, and NOT a defect.
        if (Math.abs(scrollDelta) > 200 && Math.abs(visDelta) < 20) burst.reanchor++;
        // Content moved on screen AGAINST the wheel: this one you can see.
        if (Math.abs(dirRun) >= 3) {
          var lurch = (dirRun > 0 && visDelta < -20) || (dirRun < 0 && visDelta > 20);
          if (lurch) {
            burst.visBack++;
            var vpx = Math.round(Math.abs(visDelta));
            if (vpx > burst.visBackMaxPx) burst.visBackMaxPx = vpx;
          }
        }
      }
    }
    if (el === lastTarget && lastTop !== null && Math.abs(dirRun) >= 3) {
      var delta = top - lastTop;
      if ((dirRun > 0 && delta < -BACK_PX) || (dirRun < 0 && delta > BACK_PX)) {
        burst.backwards++;
        var px = Math.abs(delta);
        if (px > burst.backMaxPx) burst.backMaxPx = Math.round(px);
      }
    }
    lastTop = top; lastTarget = el;
    touch();
  }, { passive: true, capture: true });
})();
