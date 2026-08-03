// WebEngine page/profile lifecycle, reload, download, and page-theme handling.
#include "mainwindow.h"

#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QFile>
#include <QRandomGenerator>
#include <QScreen>

#include "common.h"
#include "scheduledmessages.h"
#include "webenginenotifproxy.h"
#include "webengineprofilemanager.h"
#include "webview.h"
#include "identicons.h"
#include "portalnotification.h"
#include "notificationrules.h"
#include "performance.h"
#include "autoreply.h"
#include "translator.h"
#include "chatexport.h"
#include "notificationreply.h"
#include "messaging.h"
#include "aiassistant.h"
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>

#include <QDateTime>
#include <memory>

// Decide, once, whether desktop notifications go through the XDG portal instead
// of libnotify. The "notificationBackend" setting is "auto" (default), "portal"
// or "libnotify": auto uses the portal only inside a Flatpak sandbox where it is
// available, so a normal desktop install keeps its existing libnotify path.
bool MainWindow::usePortalNotifications() {
#if defined(Q_OS_LINUX)
  const QString backend = SettingsManager::instance()
                              .settings()
                              .value("notificationBackend", "auto")
                              .toString();
  if (backend == QLatin1String("libnotify"))
    return false;
  const bool want = (backend == QLatin1String("portal")) ||
                    (backend == QLatin1String("auto") &&
                     PortalNotification::inFlatpak());
  if (!want)
    return false;
  if (!m_portalNotifier) {
    m_portalNotifier = new PortalNotification(this);
    connect(m_portalNotifier, &PortalNotification::activated, this,
            [this](const QString &id) {
              if (auto proxy = m_portalProxies.take(id)) {
                proxy->invoke(&QWebEngineNotification::click);
                this->notificationClicked();
              }
            });
  }
  return PortalNotification::isAvailable();
#else
  return false;
#endif
}

// ── WebEngine view & page ─────────────────────────────────────────────────────
void MainWindow::createWebEngine() {
  WebEngineProfileManager::instance().applyUserSettings();

  // The central widget is now a tab bar over a stack of account views, rather
  // than a single view. With only the default account the tab bar hides itself,
  // so this is invisible until a second account is added.
  buildAccountArea();
  loadAccounts();
  // Optionally rebuild the multi-window arrangement (opt-in setting) before the
  // tabs/grid view mode is applied.
  restoreWindowLayout();

  // Restore the saved layout (tabs or grid) now that the accounts exist.
  setViewMode(static_cast<ViewMode>(
      SettingsManager::instance()
          .settings()
          .value("viewMode", static_cast<int>(ViewMode::Tabs))
          .toInt()));

  // Connection watchdog: poll the injected WebSocket health probe and reload
  // the page when WhatsApp's socket has died or gone silent.
  if (!m_connectionWatchdog) {
    m_connectionWatchdog = new QTimer(this);
    m_connectionWatchdog->setInterval(20000);
    connect(m_connectionWatchdog, &QTimer::timeout, this,
            &MainWindow::checkConnectionHealth);
    m_connectionWatchdog->start();
  }
}

// The single-account entry point, kept for the reload paths. It reloads the
// active account.
void MainWindow::createWebPage(bool offTheRecord) {
  Q_UNUSED(offTheRecord);
  if (m_activeAccount >= 0 && m_activeAccount < m_accounts.size())
    createPageFor(m_accounts[m_activeAccount].view,
                  m_accounts[m_activeAccount].id);
}

void MainWindow::createPageFor(WebView *view, const QString &accountId) {
  QWebEngineProfile *profile =
      WebEngineProfileManager::instance().profileFor(accountId);
  WebEngineProfileManager::instance().applyUserSettings();

  setNotificationPresenter(profile);

  QWebEnginePage *page = new WebEnginePage(profile, view);
  installPageBridge(page);
  const bool dark = SettingsManager::instance()
                        .settings()
                        .value("windowTheme", "light")
                        .toString() == "dark";
  const QColor pageBg = dark ? QColor(17, 27, 33)      // WhatsApp dark bg
                             : QColor(240, 240, 240);  // WhatsApp light bg
  page->setBackgroundColor(pageBg);
  // Paint the view widget itself in the same colour, so the area exposed while
  // Chromium is still repainting a live resize shows the page colour rather than
  // a flash of the default (or a mismatched theme) background.
  view->setStyleSheet(
      QStringLiteral("QWebEngineView{background:%1;}").arg(pageBg.name()));
  view->setPage(page);

  auto randomValue = QRandomGenerator::global()->generateDouble() * 300.0;
  page->setUrl(
      QUrl("https://web.whatsapp.com?v=" + QString::number(randomValue)));

  connect(profile, &QWebEngineProfile::downloadRequested,
          &m_downloadManagerWidget, &DownloadManagerWidget::downloadRequested);
  connect(page, &QWebEnginePage::fullScreenRequested, this,
          &MainWindow::fullScreenRequested);

  double currentFactor = SettingsManager::instance()
                             .settings()
                             .value("zoomFactor", 1.0)
                             .toDouble();
  view->page()->setZoomFactor(currentFactor);
}

// Buttons Whatly injects into WhatsApp's own UI need a way back into the app.
// QWebChannel is that way: it exposes exactly one object, with exactly the slots
// PageBridge declares — the page can reach nothing else.
void MainWindow::installPageBridge(QWebEnginePage *page) {
  if (!m_pageBridge) {
    m_pageBridge = new PageBridge(this);
    connect(m_pageBridge, &PageBridge::themeToggleRequested, this,
            &MainWindow::toggleTheme);
    connect(m_pageBridge, &PageBridge::privacyBlurToggleRequested, this,
            &MainWindow::togglePrivacyBlur);
    connect(m_pageBridge, &PageBridge::chatListStripToggleRequested, this,
            &MainWindow::toggleChatListStrip);
    connect(m_pageBridge, &PageBridge::zoomInRequested, this,
            &MainWindow::zoomIn);
    connect(m_pageBridge, &PageBridge::zoomOutRequested, this,
            &MainWindow::zoomOut);
    connect(m_pageBridge, &PageBridge::zoomResetRequested, this,
            &MainWindow::zoomReset);
    connect(m_pageBridge, &PageBridge::scheduledMessageFinished, this,
            [this](const QString &id, bool ok, const QString &error) {
              if (m_scheduledMessages)
                m_scheduledMessages->reportResult(id, ok, error);
            });
    connect(m_pageBridge, &PageBridge::incomingMessageReceived, this,
            &MainWindow::handleIncomingMessage);
  }
  if (!m_webChannel) {
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject(QStringLiteral("whatlyBridge"), m_pageBridge);
  }
  page->setWebChannel(m_webChannel);

  // Qt puts the transport in place, but the page still has to speak the
  // protocol: qwebchannel.js ships inside the QtWebChannel library as a
  // resource, and is injected here rather than vendored into the tree.
  QFile js(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
  if (!js.open(QIODevice::ReadOnly)) {
    qWarning() << "qwebchannel.js is missing; injected page buttons will do "
                  "nothing";
    return;
  }

  QWebEngineScript bridge;
  bridge.setName(QStringLiteral("whatly-page-bridge"));
  bridge.setSourceCode(QString::fromUtf8(js.readAll()) + QStringLiteral(R"js(
    (function connect() {
      if (typeof qt === 'undefined' || !qt.webChannelTransport) {
        setTimeout(connect, 50);   // the transport lands slightly after us
        return;
      }
      new QWebChannel(qt.webChannelTransport, function (channel) {
        window.__whatlyBridge = channel.objects.whatlyBridge;
      });
    })();
  )js"));
  bridge.setInjectionPoint(QWebEngineScript::DocumentCreation);
  bridge.setWorldId(QWebEngineScript::MainWorld);
  bridge.setRunsOnSubFrames(false);

  QWebEngineScriptCollection &scripts = page->scripts();
  const auto stale = scripts.find(bridge.name());
  for (const auto &script : stale)
    scripts.remove(script);
  scripts.insert(bridge);

  // The scheduled-message sender rides along on every load: it looks for a job
  // left in sessionStorage (which survives the navigation the send triggers)
  // and clicks Send once the chat is open, reporting back over the bridge.
  QWebEngineScript sender;
  sender.setName(QStringLiteral("whatly-scheduled-sender"));
  sender.setSourceCode(ScheduledMessages::senderScriptSource());
  sender.setInjectionPoint(QWebEngineScript::DocumentReady);
  sender.setWorldId(QWebEngineScript::MainWorld);
  sender.setRunsOnSubFrames(false);
  const auto staleSender = scripts.find(sender.name());
  for (const auto &script : staleSender)
    scripts.remove(script);
  scripts.insert(sender);

  // The attachment sender: like the scheduled sender, it looks for a job left
  // in sessionStorage (which survives the navigation that opens the chat),
  // feeds the file into WhatsApp Web's attach input, sets the caption and
  // clicks Send. Best-effort and defensive — if WhatsApp Web's DOM changes it
  // silently stops rather than breaking the page.
  QWebEngineScript attach;
  attach.setName(QStringLiteral("whatly-attachment-sender"));
  attach.setSourceCode(attachmentSenderScriptSource());
  attach.setInjectionPoint(QWebEngineScript::DocumentReady);
  attach.setWorldId(QWebEngineScript::MainWorld);
  attach.setRunsOnSubFrames(false);
  const auto staleAttach = scripts.find(attach.name());
  for (const auto &script : staleAttach)
    scripts.remove(script);
  scripts.insert(attach);

  // The auto-reply observer: watches the open conversation for new *incoming*
  // messages and reports each one over the bridge, so MainWindow can evaluate
  // the auto-reply rules and send a reply. Defensive — narrow observer, no-op
  // when nothing matches.
  QWebEngineScript observer;
  observer.setName(QStringLiteral("whatly-autoreply-observer"));
  observer.setSourceCode(autoReplyObserverScriptSource());
  observer.setInjectionPoint(QWebEngineScript::DocumentReady);
  observer.setWorldId(QWebEngineScript::MainWorld);
  observer.setRunsOnSubFrames(false);
  const auto staleObs = scripts.find(observer.name());
  for (const auto &script : staleObs)
    scripts.remove(script);
  scripts.insert(observer);

  // The name/group sender: for `--send` to a contact or group given by name
  // (or a group id), it opens the target chat from a job left in
  // sessionStorage — typing the name into WhatsApp Web's chat search and
  // clicking the result whose title matches EXACTLY (case-insensitive), so a
  // message never lands in the wrong chat — then types the text and sends.
  // A group id is opened best-effort through WhatsApp Web's internal store,
  // which is not always reachable. Defensive: if nothing matches it aborts
  // rather than guessing.
  QWebEngineScript nameSender;
  nameSender.setName(QStringLiteral("whatly-name-sender"));
  nameSender.setSourceCode(nameSenderScriptSource());
  nameSender.setInjectionPoint(QWebEngineScript::DocumentReady);
  nameSender.setWorldId(QWebEngineScript::MainWorld);
  nameSender.setRunsOnSubFrames(false);
  const auto staleName = scripts.find(nameSender.name());
  for (const auto &script : staleName)
    scripts.remove(script);
  scripts.insert(nameSender);
}

// The page-side automation for `--send --file`. Injected on every load; a no-op
// until a job appears in sessionStorage under 'whatlyAttachJob'. Verified live
// against current WhatsApp Web: type the caption into the composer first, then
// PASTE the image (a ClipboardEvent carrying the File — the hidden file-input
// path attaches the image but loses the caption), which opens the media editor
// with the caption already set; then click Send with a full pointer sequence.
QString MainWindow::attachmentSenderScriptSource() {
  return QString::fromLatin1(R"JS(
(function () {
  'use strict';
  if (window.__whatlyAttachReady) return;
  window.__whatlyAttachReady = true;
  var KEY = 'whatlyAttachJob';

  function vis(e) {
    var r = e.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  function composer() {
    var b = [].slice.call(document.querySelectorAll(
      'footer div[contenteditable="true"][role="textbox"],'
      + 'div[contenteditable="true"][data-tab]')).filter(vis);
    return b.length ? b[b.length - 1] : null;
  }
  // The preview's Send is a <button aria-label="Send"> whose icon is
  // 'wds-ic-send-filled' on current WhatsApp Web (older builds used 'send').
  function sendButton() {
    var icon = document.querySelector('[data-icon="wds-ic-send-filled"]')
      || document.querySelector('span[data-icon="send"]');
    if (icon) return icon.closest('button,[role="button"]') || icon;
    var cands = [].slice.call(document.querySelectorAll(
      'button[aria-label],[role="button"][aria-label]')).filter(function (x) {
        return /^(send|enviar)/i.test(x.getAttribute('aria-label') || '') && vis(x);
      });
    return cands.length ? cands[cands.length - 1] : null;
  }
  // WhatsApp Web's send button ignores a bare click(); it needs the full
  // pointer/mouse sequence (verified live over the remote debugger).
  function press(btn) {
    var r = btn.getBoundingClientRect();
    var cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'].forEach(function (type) {
      var Ev = type.indexOf('pointer') === 0 ? PointerEvent : MouseEvent;
      try {
        btn.dispatchEvent(new Ev(type, { bubbles: true, cancelable: true,
          clientX: cx, clientY: cy, button: 0 }));
      } catch (e) {}
    });
  }
  function toFile(b64, name, mime) {
    var bin = atob(b64);
    var arr = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
    return new File([arr], name || 'file', { type: mime || 'application/octet-stream' });
  }

  function process() {
    var raw;
    try { raw = sessionStorage.getItem(KEY); } catch (e) { return; }
    if (!raw) return;
    var job;
    try { job = JSON.parse(raw); } catch (e) {
      try { sessionStorage.removeItem(KEY); } catch (e2) {}
      return;
    }
    // Exactly one run, ever, per page: without this the caption was typed on
    // every tick / every re-entry and went out as a runaway text message.
    if (window.__whatlyAttachActive) return;
    window.__whatlyAttachActive = true;
    var deadline = job.deadline || (Date.now() + 60000);
    var captioned = false, pasted = false, done = false;
    var finish = function () {
      if (done) return;
      done = true;
      clearInterval(timer);
      try { sessionStorage.removeItem(KEY); } catch (e) {}
    };
    var timer = setInterval(function () {
      try {
        if (Date.now() > deadline) { finish(); return; }
        var comp = composer();
        if (!comp) return; // wait until the chat is open
        // 1) Caption FIRST, into the composer. WhatsApp Web's caption editor is
        //    React/Lexical-controlled: injecting into the media preview's own
        //    field does not update its state (the caption is dropped, or goes
        //    out as a separate message). Typing into the composer and THEN
        //    pasting the image keeps the text as the media caption. Verified
        //    live over the remote debugger.
        if (job.caption && !captioned) {
          comp.focus();
          try { document.execCommand('insertText', false, job.caption); } catch (e) {}
          captioned = true;
          return;
        }
        // 2) Paste the image as a File (a ClipboardEvent, NOT the file input):
        //    this opens the media editor with the caption already in place.
        if (!pasted) {
          var dt = new DataTransfer();
          dt.items.add(toFile(job.b64, job.name, job.mime));
          comp.focus();
          comp.dispatchEvent(new ClipboardEvent('paste',
            { clipboardData: dt, bubbles: true, cancelable: true }));
          pasted = true;
          return; // let the media editor render
        }
        // 3) Send only once the media EDITOR is unambiguously open. Its toolbar
        //    carries icons that never exist in the plain composer (crop
        //    'scissors', close 'x-alt'); gating on one of those guarantees we
        //    press the EDITOR's Send — not the composer's, which shares the same
        //    send icon and would fire the caption as a separate text message.
        var editorOpen = !!document.querySelector(
          '[data-icon="scissors"], [data-icon="x-alt"]');
        var btn = sendButton();
        if (!editorOpen || !btn) return;
        press(btn);
        finish();
      } catch (e) { /* keep trying until the deadline */ }
    }, 600);
  }

  // Poll for the job (it may be set just before or after this script loads);
  // process() self-guards so only one run ever starts.
  var boot = setInterval(function () {
    try { if (sessionStorage.getItem(KEY)) { clearInterval(boot); process(); } }
    catch (e) {}
  }, 500);
  setTimeout(function () { try { clearInterval(boot); } catch (e) {} }, 20000);
})();
)JS");
}

// The page-side automation for `--send` to a contact/group by name (or a group
// id). Injected on every load; a no-op until a job appears in sessionStorage
// under 'whatlyNameJob'. It opens the target chat, then types the text and
// sends. Best-effort and defensive — it aborts rather than send to the wrong
// chat, and reports the outcome over the bridge so C++ can notify the user.
// NOTE: written against current WhatsApp Web selectors but NOT yet verified
// live; treat as work-in-progress.
QString MainWindow::nameSenderScriptSource() {
  return QString::fromLatin1(R"JS(
(function () {
  'use strict';
  if (window.__whatlyNameReady) return;
  window.__whatlyNameReady = true;
  var KEY = 'whatlyNameJob';

  function report(ok, err) {
    try {
      if (window.__whatlyBridge && window.__whatlyBridge.scheduledMessageResult)
        window.__whatlyBridge.scheduledMessageResult('name', !!ok, String(err || ''));
    } catch (e) { /* bridge not ready; C++ falls back to an optimistic notice */ }
  }
  function vis(e) {
    var r = e.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  function norm(s) { return (s || '').replace(/\s+/g, ' ').trim().toLowerCase(); }

  function searchBox() {
    // The chat-list search field (not the composer). Current WhatsApp Web uses a
    // plain <input> (data-tab="3"); older builds used a contenteditable div, so
    // fall back to that. Verified live: it is an <input aria-label="Search…">.
    return document.querySelector('input[aria-label*="Search" i]')
      || document.querySelector('input[aria-label*="Buscar" i]')
      || document.querySelector('input[data-tab="3"]')
      || document.querySelector('div[contenteditable="true"][data-tab="3"]');
  }
  function setText(el, text) {
    el.focus();
    // A React-controlled <input> ignores execCommand; drive its native value
    // setter then fire 'input'. A contenteditable field uses execCommand.
    if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA') {
      var proto = el.tagName === 'INPUT'
        ? window.HTMLInputElement.prototype : window.HTMLTextAreaElement.prototype;
      try {
        Object.getOwnPropertyDescriptor(proto, 'value').set.call(el, text);
      } catch (e) { el.value = text; }
      el.dispatchEvent(new Event('input', { bubbles: true }));
    } else {
      try { document.execCommand('selectAll', false, null); } catch (e) {}
      try { document.execCommand('insertText', false, text); } catch (e) {}
      el.dispatchEvent(new InputEvent('input', { bubbles: true }));
    }
  }
  // A chat row in the side list; its visible title lives in a span[title].
  // Current WhatsApp Web marks rows with role="row" (older builds: "listitem").
  function matchingResult(query) {
    var rows = [].slice.call(document.querySelectorAll(
      '#pane-side [role="row"], #side [role="row"],'
      + '#pane-side [role="listitem"], #side [role="listitem"]')).filter(vis);
    for (var i = 0; i < rows.length; i++) {
      var t = rows[i].querySelector('span[title]');
      if (t && norm(t.getAttribute('title') || t.textContent) === query)
        return rows[i];
    }
    return null;
  }
  function clickRow(row) {
    var target = row.querySelector('span[title]') || row;
    var r = target.getBoundingClientRect();
    var cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'].forEach(function (type) {
      var Ev = type.indexOf('pointer') === 0 ? PointerEvent : MouseEvent;
      try {
        target.dispatchEvent(new Ev(type, { bubbles: true, cancelable: true,
          clientX: cx, clientY: cy, button: 0 }));
      } catch (e) {}
    });
  }
  function composer() {
    var b = [].slice.call(document.querySelectorAll(
      'footer div[contenteditable="true"][role="textbox"],'
      + 'div[contenteditable="true"][data-tab]')).filter(vis);
    return b.length ? b[b.length - 1] : null;
  }
  // Works for both the composer's Send and the media editor's Send (the editor
  // is not inside <footer>), so the attachment path can reuse it.
  function sendButton() {
    var icon = document.querySelector('[data-icon="wds-ic-send-filled"]')
      || document.querySelector('span[data-icon="send"]');
    if (icon) return icon.closest('button,[role="button"]') || icon;
    var cands = [].slice.call(document.querySelectorAll(
      'button[aria-label],[role="button"][aria-label]')).filter(function (x) {
        return /^(send|enviar)/i.test(x.getAttribute('aria-label') || '') && vis(x);
      });
    return cands.length ? cands[cands.length - 1] : null;
  }
  function press(btn) {
    var r = btn.getBoundingClientRect();
    var cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'].forEach(function (type) {
      var Ev = type.indexOf('pointer') === 0 ? PointerEvent : MouseEvent;
      try {
        btn.dispatchEvent(new Ev(type, { bubbles: true, cancelable: true,
          clientX: cx, clientY: cy, button: 0 }));
      } catch (e) {}
    });
  }
  // Resolve a group id to its display title through WhatsApp Web's internal
  // module loader (Meta's require(), not webpack), verified live:
  // require('WAWebChatCollection').ChatCollection.get(jid).formattedTitle.
  // The by-name search path then opens it like any other chat — far more robust
  // than driving an internal "open chat" action. Returns '' if unavailable.
  function groupTitleForId(id) {
    try {
      if (typeof window.require !== 'function') return '';
      var jid = id.indexOf('@') === -1 ? (id + '@g.us') : id;
      var mod = window.require('WAWebChatCollection');
      var CC = mod && mod.ChatCollection;
      if (!CC || !CC.get) return '';
      var chat = CC.get(jid);
      return chat ? (chat.formattedTitle || chat.name || '') : '';
    } catch (e) { return ''; }
  }
  function toFile(b64, name, mime) {
    var bin = atob(b64);
    var arr = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
    return new File([arr], name || 'file', { type: mime || 'application/octet-stream' });
  }

  function process() {
    var raw;
    try { raw = sessionStorage.getItem(KEY); } catch (e) { return; }
    if (!raw) return;
    if (window.__whatlyNameActive) return;
    window.__whatlyNameActive = true;
    var job;
    try { job = JSON.parse(raw); } catch (e) {
      try { sessionStorage.removeItem(KEY); } catch (e2) {}
      return;
    }
    var deadline = job.deadline || (Date.now() + 45000);
    var query = norm(job.query);      // normalised title, for the exact match
    var searchText = job.query;       // what we type into the search box
    var opened = false, searched = false, groupTried = false, typed = false, done = false;
    var captioned = false, pasted = false; // attachment sub-steps
    var finish = function (ok, err) {
      if (done) return;
      done = true;
      clearInterval(timer);
      try { sessionStorage.removeItem(KEY); } catch (e) {}
      // Release the guard so a later --send (same page, no reload) is picked up.
      window.__whatlyNameActive = false;
      report(ok, err);
    };

    var timer = setInterval(function () {
      try {
        if (Date.now() > deadline) {
          finish(false, 'timeout: no exact match for "' + (job.query || '') + '"');
          return;
        }
        // Phase A: open the target chat.
        if (!opened) {
          // A group id: resolve it to its title once, then fall through to the
          // same exact-title search used for a name.
          if (job.kind === 'groupid' && !groupTried) {
            groupTried = true;
            var title = groupTitleForId(job.query);
            if (!title) {
              finish(false, 'could not resolve the group id (open WhatsApp Web '
                          + 'fully first, or send by the group name)');
              return;
            }
            searchText = title;
            query = norm(title);
          }
          var box = searchBox();
          if (!box) return; // side panel not ready yet
          if (!searched) { setText(box, searchText); searched = true; return; }
          var row = matchingResult(query);
          if (!row) return; // keep waiting for an EXACT match until the deadline
          clickRow(row);
          opened = true;
          return; // let the conversation open
        }
        // Phase B: the chat is open once its composer is present.
        var comp = composer();
        if (!comp) return;

        // B2: attachment. Same caption-first, paste-as-File, send-from-the-media
        // -EDITOR choreography as the attachment sender (verified live there): a
        // caption typed into the composer stays attached to the media, and
        // gating Send on an editor-only icon avoids firing the composer's Send.
        if (job.attach && job.attach.b64) {
          if (job.text && !captioned) {
            comp.focus();
            try { document.execCommand('insertText', false, job.text); } catch (e) {}
            captioned = true;
            return;
          }
          if (!pasted) {
            var dt = new DataTransfer();
            dt.items.add(toFile(job.attach.b64, job.attach.name, job.attach.mime));
            comp.focus();
            comp.dispatchEvent(new ClipboardEvent('paste',
              { clipboardData: dt, bubbles: true, cancelable: true }));
            pasted = true;
            return; // let the media editor render
          }
          var editorOpen = !!document.querySelector('[data-icon="scissors"], [data-icon="x-alt"]');
          var abtn = sendButton();
          if (!editorOpen || !abtn) return;
          press(abtn);
          setTimeout(function () { finish(true, ''); }, 400);
          return;
        }

        // B1: plain text. Type it then send.
        if (job.text && !typed) {
          comp.focus();
          try { document.execCommand('insertText', false, job.text); } catch (e) {}
          typed = true;
          return;
        }
        var btn = sendButton();
        if (!btn) return;
        press(btn);
        setTimeout(function () { finish(true, ''); }, 400);
      } catch (e) { /* keep trying until the deadline */ }
    }, 700);
  }

  // Poll indefinitely: unlike the scheduled/attachment senders (which navigate,
  // so a fresh script re-injects on reload), a by-name send leaves its job in
  // sessionStorage on the ALREADY-loaded page — which may be hours old. A run
  // clears __whatlyNameActive on finish, so back-to-back sends are handled.
  setInterval(function () {
    try {
      if (sessionStorage.getItem(KEY) && !window.__whatlyNameActive) process();
    } catch (e) {}
  }, 500);
})();
)JS");
}

// Watches the open conversation for NEW incoming messages and reports each over
// the bridge (window.__whatlyBridge.incomingMessage). Best-effort and defensive:
// current WhatsApp Web obfuscates its class names (no more .message-in), so
// direction is read from bubble position — an incoming bubble sits left of the
// conversation's centre, an outgoing one right. Messages already on screen are
// baselined so history is never replayed, and a bulk insertion (a chat switch)
// re-baselines instead of replying. Verified live against current WhatsApp Web.
QString MainWindow::autoReplyObserverScriptSource() {
  return QString::fromLatin1(R"JS(
(function () {
  'use strict';
  if (window.__whatlyAutoReplyReady) return;
  window.__whatlyAutoReplyReady = true;
  var ROW = 'div[role="row"]';
  var seen = Object.create(null);

  function textSpan(r) {
    return r.querySelector('.selectable-text.copyable-text, span.selectable-text');
  }
  function keyOf(r) {
    var e = r.querySelector('[data-id]') || r;
    return e.getAttribute('data-id') || (r.textContent || '').slice(0, 80);
  }
  function isIncoming(r) {
    // Left of the conversation centre => incoming; right => our own message.
    var main = document.querySelector('#main');
    var t = textSpan(r);
    if (!main || !t) return false;
    var mr = main.getBoundingClientRect();
    var br = t.getBoundingClientRect();
    return (br.left + br.width / 2) < (mr.left + mr.width / 2);
  }
  function markSeen() {
    try {
      var rows = document.querySelectorAll('#main ' + ROW);
      for (var i = 0; i < rows.length; i++) seen[keyOf(rows[i])] = 1;
    } catch (e) {}
  }
  function report(text) {
    try {
      if (text && window.__whatlyBridge && window.__whatlyBridge.incomingMessage)
        window.__whatlyBridge.incomingMessage(String(text));
    } catch (e) {}
  }

  markSeen(); // baseline: never reply to messages already on screen

  var mo = new MutationObserver(function (muts) {
    try {
      var rows = [];
      muts.forEach(function (m) {
        for (var i = 0; i < m.addedNodes.length; i++) {
          var n = m.addedNodes[i];
          if (n.nodeType !== 1) continue;
          if (n.matches && n.matches(ROW)) rows.push(n);
          if (n.querySelectorAll) {
            var inner = n.querySelectorAll(ROW);
            for (var j = 0; j < inner.length; j++) rows.push(inner[j]);
          }
        }
      });
      var msgs = rows.filter(function (r) { var t = textSpan(r); return t && t.innerText.trim(); });
      if (!msgs.length) return;
      // A burst is a chat being loaded/switched, not live traffic: re-baseline.
      if (msgs.length > 3) { markSeen(); return; }
      for (var k = 0; k < msgs.length; k++) {
        var key = keyOf(msgs[k]);
        if (seen[key]) continue;
        seen[key] = 1;
        if (isIncoming(msgs[k]))
          report(textSpan(msgs[k]).innerText.trim());
      }
    } catch (e) {}
  });
  try {
    mo.observe(document.body, { childList: true, subtree: true });
  } catch (e) {}
})();
)JS");
}

void MainWindow::handleIncomingMessage(const QString &text) {
  const QString reply = AutoReply::replyFor(text);
  if (reply.isEmpty() || !m_webEngine || !m_webEngine->page())
    return;
  // Type the reply into the open conversation's composer and press Send. Reuses
  // the same composer-insert + pointer-press technique as the other senders.
  static const QString kTemplate = QString::fromLatin1(R"JS(
(function () {
  'use strict';
  try {
    var TEXT = %1;
    function vis(e){var r=e.getBoundingClientRect();return r.width>0&&r.height>0;}
    var box = [].slice.call(document.querySelectorAll(
      'footer div[contenteditable="true"][role="textbox"],'
      + 'div[contenteditable="true"][data-tab]')).filter(vis).pop();
    if (!box) return;
    box.focus();
    document.execCommand('insertText', false, TEXT);
    setTimeout(function () {
      var icon = document.querySelector('[data-icon="wds-ic-send-filled"]')
        || document.querySelector('span[data-icon="send"]');
      var btn = icon ? (icon.closest('button,[role="button"]') || icon) : null;
      if (!btn) return;
      var r = btn.getBoundingClientRect(), cx = r.left + r.width/2, cy = r.top + r.height/2;
      ['pointerdown','mousedown','pointerup','mouseup','click'].forEach(function (t) {
        var Ev = t.indexOf('pointer') === 0 ? PointerEvent : MouseEvent;
        try { btn.dispatchEvent(new Ev(t, { bubbles:true, cancelable:true, clientX:cx, clientY:cy, button:0 })); } catch (e) {}
      });
    }, 250);
  } catch (e) { /* never break the page */ }
})();
)JS");
  // Encode the reply as a safe JS string literal via JSON.
  const QString literal = QString::fromUtf8(
      QJsonDocument(QJsonArray{reply}).toJson(QJsonDocument::Compact));
  // literal is ["reply"]; take the element to get a quoted JS string.
  const QString jsStr =
      literal.mid(1, literal.size() - 2); // strip [ ]
  m_webEngine->page()->runJavaScript(kTemplate.arg(jsStr));
}

void MainWindow::setNotificationPresenter(QWebEngineProfile *profile) {
  if (m_webengine_notifier_popup != nullptr) {
    m_webengine_notifier_popup->close();
    m_webengine_notifier_popup->deleteLater();
  }

  m_webengine_notifier_popup = new NotificationPopup(m_webEngine);
  connect(m_webengine_notifier_popup, &NotificationPopup::notification_clicked,
          this, [this]() { notificationClicked(); });

  profile->setNotificationPresenter(
      [&](std::unique_ptr<QWebEngineNotification> notification) {
        QSettings &settings = SettingsManager::instance().settings();
        if (settings.value("disableNotificationPopups", false).toBool())
          return;

        // Do Not Disturb / keyword rules: suppress popups inside the DND window
        // unless a highlight keyword matches. Unread badges still update because
        // that happens elsewhere, on the page title.
        if (notification &&
            !NotificationRules::shouldNotify(QDateTime::currentDateTime(),
                                             notification->title(),
                                             notification->message()))
          return;

        int notificationCombo = settings.value("notificationCombo", 0).toInt();
        int timeout = settings.value("notificationTimeOut", 9000).toInt();

        if (notificationCombo == 0) {
#ifdef Q_OS_LINUX
          // Wrap the notification in the lifetime-managing proxy up front so both
          // the portal and libnotify paths can use it safely.
          auto proxy = WebEngineNotifProxy::create(std::move(notification));

          // Flatpak-friendly path: dispatch through the XDG portal when it is the
          // chosen/available backend, routing the activation back to this
          // notification. Falls through to libnotify when the send fails.
          if (usePortalNotifications() && m_portalNotifier) {
            const QString id =
                QStringLiteral("whatly-%1").arg(++m_portalNotifSeq);
            if (m_portalNotifier->send(id, proxy->notif->title(),
                                       proxy->notif->message())) {
              m_portalProxies.insert(id, proxy);
              proxy->invoke(&QWebEngineNotification::show);
              return;
            }
          }
          // Build the per-contact identicon once; both the inline-reply path
          // and the libnotify path below reuse it.
          QPixmap pix = [proxy](auto img) {
            return Identicons::colorCount(img) > 2
                ? QPixmap::fromImage(img)
                : Identicons::letterTile(proxy->notif->title(), QSize(128, 128));
          } (proxy->notif->icon());

          // Inline-reply path (idea #2): where the server supports it and the
          // user has it on, post through org.freedesktop.Notifications with a
          // reply field so the message can be answered without opening a window.
          if (ensureInlineReply()) {
            const QString chat = proxy->notif->title();
            const quint32 id = m_notificationReply->notify(
                QStringLiteral("Whatly"), chat, proxy->notif->message(),
                Identicons::clipRRect(pix).toImage(), timeout, tr("Reply"),
                tr("Reply to %1…").arg(chat));
            if (id) {
              m_replyNotifs.insert(id, qMakePair(proxy, chat));
              proxy->invoke(&QWebEngineNotification::show);
              return;
            }
          }

          auto ntf = notify(proxy->notif->title(), proxy->notif->message(), timeout);
          ntf->setHint("image-data", notificationImageHint(
                                        Identicons::clipRRect(pix) /* for eyecandy */));
          connect(ntf.get(), &Notification::Event::actionInvoked, this,
              [this, proxy](const QString & action) {
                if (action != "open") return;
                proxy->invoke(&QWebEngineNotification::click);
                this->notificationClicked();
              });

          connect(ntf.get(), &Notification::Event::closed, this,
              [this, proxy](Notification::ClosingReason reason) {
                proxy->invoke(&QWebEngineNotification::close);
              });

          ntf->show();
          proxy->invoke(&QWebEngineNotification::show);
          return;
#else
          // Native notifications via the system tray (toast notifications on
          // Windows 10+); falls back to the popup below when no tray is
          // available.
          if (m_systemTrayIcon && QSystemTrayIcon::supportsMessages()) {
            // Use Proxy to manage lifecycle of QWebEngineNotification safely
            auto proxy = WebEngineNotifProxy::create(std::move(notification));
            // Use locally generated identicon when
            // QWebEngine (or whatsapp) passes blank
            // image
            QPixmap pix = [proxy](auto img) {
              return Identicons::colorCount(img) > 2
                  ? QPixmap::fromImage(img)
                  : Identicons::letterTile(proxy->notif->title(), QSize(128, 128));
            } (proxy->notif->icon());
            // A new toast replaces the visible one, so route messageClicked
            // to the handler of the most recent notification only.
            disconnect(m_trayNotificationClickConnection);
            m_trayNotificationClickConnection = connect(
                m_systemTrayIcon, &QSystemTrayIcon::messageClicked, this,
                [this, proxy]() {
                  proxy->invoke(&QWebEngineNotification::click);
                  this->notificationClicked();
                });
            m_systemTrayIcon->showMessage(
                proxy->notif->title(), proxy->notif->message(),
                QIcon(Identicons::clipRRect(pix) /* for eyecandy */), timeout);
            proxy->invoke(&QWebEngineNotification::show);
            return;
          }
#endif
        }

        if (!m_webengine_notifier_popup) {
          qWarning() << "Popup is not available!";
          return;
        }

        m_webengine_notifier_popup->setMinimumWidth(300);
        // The screen this window lives on, not the primary one. On a
        // multi-monitor desk the popup used to appear on whichever screen the
        // system called primary, which is often not the one the user is
        // looking at. QWidget::screen() already falls back to the primary
        // screen when the window has no handle yet.
        QScreen *screen = this->screen();
        if (!screen) {
          const auto screens = QGuiApplication::screens();
          if (!screens.isEmpty()) {
            screen = screens.first();
          } else {
            qWarning() << "showNotification: unable to get any screen";
            return;
          }
        }
        m_webengine_notifier_popup->present(screen, notification);
      });
}

// ── Reload & load events ──────────────────────────────────────────────────────

void MainWindow::doAppReload() {
  if (m_webEngine->page())
    m_webEngine->page()->disconnect();
  createWebPage(false);
}

void MainWindow::doReload(bool byPassCache, bool isAskedByCLI,
                          bool byLoadingQuirk) {
  if (byLoadingQuirk) {
    m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache,
                                   byPassCache);
    return;
  }

  if (m_lockWidget && !m_lockWidget->getIsLocked()) {
    this->showNotification(QApplication::applicationDisplayName(),
                           QObject::tr("Reloading..."));
  } else {
    ensureLockVisible();   // give the user the unlock screen, not just a refusal
    QString error = tr("Unlock to Reload the App.");
    if (isAskedByCLI) {
      this->showNotification(QApplication::applicationDisplayName() + tr("| Error"),
                             error);
    } else {
      QMessageBox::critical(this, QApplication::applicationDisplayName() + tr("| Error"),
                            error);
    }
    this->show();
    return;
  }
  m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache,
                                 byPassCache);
}

void MainWindow::checkConnectionHealth() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  // Don't reload while the app is locked (would fight the lock screen).
  if (m_lockWidget && m_lockWidget->getIsLocked())
    return;
  // Cooldown: never auto-reload more than once per 60s, to avoid reload storms.
  if (m_lastWatchdogReload.isValid() && m_lastWatchdogReload.elapsed() < 60000)
    return;

  m_webEngine->page()->runJavaScript(
      QStringLiteral("(typeof window.__whatlyWsState==='function')?"
                     "window.__whatlyWsState():'idle'"),
      [this](const QVariant &result) {
        const QString state = result.toString();

        // Reflect the connection in the tray icon (see getTrayIcon). "ok" is
        // connected; "stuck" and "idle" (still connecting / offline) are not.
        const bool connected = state == QLatin1String("ok");
        if (connected != m_trayConnected) {
          m_trayConnected = connected;
          updateTrayUnread();   // repaint the tray icon in the new state
          // #208: spell the state out in the tray tooltip too, so a silent
          // disconnect after a suspend/resume is noticeable, not just a dimmer
          // icon.
          if (m_systemTrayIcon)
            m_systemTrayIcon->setToolTip(
                connected ? QApplication::applicationDisplayName()
                          : tr("Waiting for network…"));
        }

        if (state == QLatin1String("ok")) {
          // Connection healthy again: reset so a future, unrelated hang gets a
          // fresh set of recovery attempts.
          m_watchdogStrikes = 0;
          m_watchdogReloads = 0;
          m_watchdogGaveUp = false;
          return;
        }

        if (state != QLatin1String("stuck")) {
          // "idle": still connecting or offline — nothing a reload would fix.
          m_watchdogStrikes = 0;
          return;
        }

        // Require two consecutive "stuck" reports (~20-40s) before acting, so a
        // brief reconnect gap or a momentarily idle socket is not mistaken for
        // a hang.
        if (++m_watchdogStrikes < 2)
          return;
        m_watchdogStrikes = 0;

        // Cap recovery at 3 reloads per hang episode. If the connection is still
        // stuck after 3 reloads the cause is not something a reload fixes (no
        // disk space, network down, ...), so stop hammering — repeated reloads
        // are expensive and pointless. The counter resets once the connection
        // reports healthy again (state == "ok").
        if (m_watchdogReloads >= 3) {
          if (!m_watchdogGaveUp) {
            m_watchdogGaveUp = true;
            qWarning() << "Connection watchdog: still stuck after 3 reloads, "
                          "giving up until the connection recovers.";
          }
          return;
        }

        ++m_watchdogReloads;
        m_lastWatchdogReload.restart();
        qWarning() << "Connection watchdog: WhatsApp WebSocket stuck, reload"
                   << m_watchdogReloads << "of 3.";
        if (m_webEngine)
          m_webEngine->triggerPageAction(QWebEnginePage::ReloadAndBypassCache);
      });
}

void MainWindow::handleLoadFinished(bool loaded) {
  if (loaded) {
    qDebug() << "Loaded";
    // The page rendered: disarm the start-up crash watch and reset any safe-
    // rendering recovery level, so a one-off crash does not stick (issue #3).
    Performance::markStartupSucceeded();
    m_watchdogStrikes = 0; // fresh document, start clean
    checkLoadedCorrectly();
    updatePageTheme();
    handleZoom();
    if (m_settingsWidget != nullptr)
      m_settingsWidget->refresh();
    // WhatsApp Web is up: begin (or continue) the scheduled-message queue, so
    // anything that fell due while the app was closed goes out now. start() is
    // idempotent and only sends once the page has loaded.
    if (m_scheduledMessages != nullptr)
      m_scheduledMessages->start();
    // The page is up, so window.Debug.VERSION is now readable: cache the
    // WhatsApp Web version of whichever account just finished, for its tooltip.
    if (auto *loadedView = qobject_cast<WebView *>(sender()))
      captureAccountVersion(loadedView);
    else if (auto *active = qobject_cast<WebView *>(m_webEngine))
      captureAccountVersion(active);
    // Warn once if this build's Qt WebEngine cannot handle H.264/MP4 (the
    // official/aqt Qt used by the portable builds ships no proprietary codecs),
    // so a failed video attach is explained rather than mysterious (issue #34).
    checkMediaCodecs();
  }
}

void MainWindow::checkMediaCodecs() {
  if (m_codecCheckDone || !m_webEngine || !m_webEngine->page())
    return;
  m_codecCheckDone = true; // run the probe at most once per session
  if (SettingsManager::instance()
          .settings()
          .value("mediaCodecNoticeShown", false)
          .toBool())
    return; // already told the user once

  // Ask the engine whether it can play H.264 (what WhatsApp needs for MP4).
  m_webEngine->page()->runJavaScript(
      QStringLiteral(
          "(function(){try{var v=document.createElement('video');"
          "return v.canPlayType('video/mp4; codecs=\"avc1.42E01E\"')||'';}"
          "catch(e){return 'err';}})();"),
      [this](const QVariant &v) {
        const QString r = v.toString();
        if (r != QLatin1String("")) // supported (or probe failed): no notice
          return;
        SettingsManager::instance().settings().setValue("mediaCodecNoticeShown",
                                                        true);
        if (m_webEngine && m_webEngine->page())
          m_webEngine->page()->runJavaScript(Translate::toastScript(
              tr("This build cannot send H.264/MP4 videos: its browser engine "
                 "was built without the proprietary codecs. Photos and WebM/VP9 "
                 "videos work; for MP4, use a distro/native package built with "
                 "the codecs. (Click to dismiss.)"),
              /*persistent=*/true));
      });
}

void MainWindow::checkLoadedCorrectly() {
  if (!m_webEngine || !m_webEngine->page())
    return;

  m_webEngine->page()->runJavaScript(
      "document.querySelector('body').className",
      [this](const QVariant &result) {
        if (result.toString().contains("page-version", Qt::CaseInsensitive)) {
          qDebug() << "Test 1 found" << result.toString();
          m_webEngine->page()->runJavaScript(
              "document.getElementsByTagName('body')[0].innerText = ''");
          loadingQuirk("test1");
        } else if (m_webEngine->title().contains("Error",
                                                 Qt::CaseInsensitive)) {
          Utils::delete_cache(m_webEngine->page()->profile()->cachePath());
          Utils::delete_cache(
              m_webEngine->page()->profile()->persistentStoragePath());
          SettingsManager::instance().settings().setValue("useragent",
                                                          defaultUserAgentStr);
          Utils::DisplayExceptionErrorDialog(
              "test1 handleWebViewTitleChanged(title) title: Error, "
              "Resetting UA, Quiting!\nUA: " +
              SettingsManager::instance()
                  .settings()
                  .value("useragent", "DefaultUA")
                  .toString());
          m_quitAction->trigger();
        } else {
          qDebug() << "Test 1 loaded correctly, value:" << result.toString();
        }
      });
}

void MainWindow::loadingQuirk(const QString &test) {
  if (m_correctlyLoadedRetries > -1) {
    qWarning() << test << "checkLoadedCorrectly()/loadingQuirk()/doReload()"
               << m_correctlyLoadedRetries;
    doReload(false, false, true);
    m_correctlyLoadedRetries--;
  } else {
    Utils::delete_cache(m_webEngine->page()->profile()->cachePath());
    Utils::delete_cache(
        m_webEngine->page()->profile()->persistentStoragePath());
    SettingsManager::instance().settings().setValue("useragent",
                                                    defaultUserAgentStr);
    Utils::DisplayExceptionErrorDialog(
        test +
        " checkLoadedCorrectly()/loadingQuirk() reload retries 0, Resetting "
        "UA, Quiting!\nUA: " +
        SettingsManager::instance()
            .settings()
            .value("useragent", "DefaultUA")
            .toString());
    m_quitAction->trigger();
  }
}

// ── Page theme ────────────────────────────────────────────────────────────────

void MainWindow::updatePageTheme() {
  if (!m_webEngine || !m_webEngine->page())
    return;

  const bool dark = SettingsManager::instance()
                        .settings()
                        .value("windowTheme", "light")
                        .toString() == "dark";

  // Sequence reverse-engineered from WhatsApp Web's own theme-toggle logic:
  //
  // 1. WA module calls via global require():
  //      WAWebUserPrefsGeneral.setSystemThemeMode(false)  -- disable "follow OS"
  //      WAWebUserPrefsGeneral.setTheme(theme)            -- persist preference
  //      WAWebThemeContext.applyThemeToUI(theme)          -- repaint UI
  //      WAWebSystemTheme.theme = theme                   -- update system ref
  //
  // 2. React class component setState -- walk fiber ancestors of .app-wrapper-web
  //    upward via .return until we find the component whose state has both
  //    .theme and .systemThemeMode, then setState({theme, systemThemeMode:false}).
  //    Fall back to forceUpdate() on ancestors if that component is not found.
  //
  // 3. DOM attributes + localStorage -- persists across reloads, covers
  //    any CSS-only observers.
  const QString js = QString(R"js(
    (function(theme) {
      var isDark = theme === 'dark';

      // ── 1. WA module calls via global require() ───────────────────────────
      if (typeof require === 'function') {
        try {
          var up = require('WAWebUserPrefsGeneral');
          if (up) {
            if (typeof up.setSystemThemeMode === 'function') up.setSystemThemeMode(false);
            if (typeof up.setTheme          === 'function') up.setTheme(theme);
          }
        } catch (e) {}
        try {
          var tc = require('WAWebThemeContext');
          if (tc && typeof tc.applyThemeToUI === 'function') tc.applyThemeToUI(theme);
        } catch (e) {}
        try {
          var st = require('WAWebSystemTheme');
          if (st) st.theme = theme;
        } catch (e) {}
      }

      // ── 2. React class component setState (upward fiber walk) ─────────────
      try {
        var wrapper = document.querySelector('.app-wrapper-web');
        var rk = wrapper && Object.keys(wrapper).find(function(k) {
          return k.startsWith('__reactFiber') || k.startsWith('__reactInternalInstance');
        });
        if (rk) {
          var fiber = wrapper[rk];
          var found = false;
          while (fiber) {
            var sn = fiber.stateNode;
            if (sn && sn.state &&
                sn.state.theme !== undefined &&
                sn.state.systemThemeMode !== undefined &&
                typeof sn.setState === 'function') {
              sn.setState({theme: theme, systemThemeMode: false});
              found = true;
              break;
            }
            fiber = fiber.return;
          }
          if (!found) {
            fiber = wrapper[rk];
            var count = 0;
            while (fiber && count < 10) {
              if (fiber.stateNode && typeof fiber.stateNode.forceUpdate === 'function') {
                try { fiber.stateNode.forceUpdate(); } catch (e) {}
                count++;
              }
              fiber = fiber.return;
            }
          }
        }
      } catch (e) {}

      // ── 3. DOM attributes + localStorage ─────────────────────────────────
      var root = document.documentElement;
      root.setAttribute('data-theme',      theme);
      root.setAttribute('data-color-mode', theme);
      root.style.colorScheme = theme;
      document.body.classList.toggle('dark', isDark);

      localStorage.setItem('theme', theme);
      localStorage.removeItem('system-theme-mode');
      try {
        window.dispatchEvent(new StorageEvent('storage', {
          key: 'theme', newValue: theme,
          storageArea: localStorage, url: location.href
        }));
      } catch (e) {}
    })('%1');
  )js").arg(dark ? "dark" : "light");

  m_webEngine->page()->runJavaScript(js);
}

QString MainWindow::getPageTheme() const {
  static QString theme = "web"; // implies light
  if (m_webEngine && m_webEngine->page()) {
    // Read back from the same localStorage key WhatsApp writes to, so we
    // always get the authoritative value regardless of DOM structure changes.
    m_webEngine->page()->runJavaScript(
        "(function(){"
        "  var v = localStorage.getItem('theme');"
        "  if (v === null) return '';"              // WhatsApp has not stored one
        "  try { v = JSON.parse(v); } catch(e) {}"  // handle both 'dark' and '"dark"'
        "  if (v === 'dark' || v === 'light') return v;"
        "  return '';"                              // anything else: no opinion
        "})();",
        [=](const QVariant &result) {
          const QString value = result.toString();
          // Only persist a theme the page actually reported. This ran on the
          // way out, while the page is being torn down, and runJavaScript then
          // calls back with an empty result — which the old code folded into
          // "light" and saved as if the user had chosen it. Every clean exit
          // therefore reset the theme, and the loss only showed up on the next
          // launch, which is why it looked like a startup bug.
          if (value != QLatin1String("dark") && value != QLatin1String("light"))
            return;
          theme = value;
          SettingsManager::instance().settings().setValue("windowTheme", theme);
        });
  }
  return theme;
}

// ── Fullscreen ────────────────────────────────────────────────────────────────

void MainWindow::fullScreenRequested(QWebEngineFullScreenRequest request) {
  if (request.toggleOn()) {
    windowStateBeforeFullScreen = this->windowState();
    this->hide();
    m_webEngine->showFullScreen();
    m_webEngine->setWindowState(Qt::WindowFullScreen);
    this->setWindowState(Qt::WindowFullScreen);
    this->show();
  } else {
    this->hide();
    m_webEngine->showNormal();
    this->setWindowState(windowStateBeforeFullScreen);
    this->show();
  }
  request.accept();
}

// ── Misc web engine helpers ───────────────────────────────────────────────────

void MainWindow::toggleMute(const bool &checked) {
  // Mute every account, not just the visible one — a background account's call
  // tone or notification sound should go quiet too.
  for (const Account &account : m_accounts)
    if (account.view && account.view->page())
      account.view->page()->setAudioMuted(checked);

  SettingsManager::instance().settings().setValue("muteAudio", checked);
  // Keep the tray action and the Settings checkbox showing the same state,
  // wherever the toggle came from.
  if (m_muteAction && m_muteAction->isChecked() != checked)
    m_muteAction->setChecked(checked);
  if (m_settingsWidget)
    m_settingsWidget->muteAudioSetChecked(checked);
}

// ── Inline translation (idea #6) ────────────────────────────────────────────

// The target language: the configured code, or the base of the app UI language.
QString MainWindow::appTargetLanguage() const {
  const QString appLang = SettingsManager::instance()
                              .settings()
                              .value(QStringLiteral("language"))
                              .toString();
  return Translate::effectiveTargetLang(
      Translate::targetLang(),
      appLang.isEmpty() ? QLocale::system().name() : appLang);
}

void MainWindow::translateSelection() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      Translate::readSelectionScript(), [this](const QVariant &v) {
        runTranslation(v.toString(), /*intoComposer=*/false);
      });
}

void MainWindow::translateComposer() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      Translate::readComposerScript(), [this](const QVariant &v) {
        runTranslation(v.toString(), /*intoComposer=*/true);
      });
}

void MainWindow::runTranslation(const QString &text, bool intoComposer) {
  const QString trimmed = text.trimmed();
  auto toast = [this](const QString &msg) {
    if (m_webEngine && m_webEngine->page())
      m_webEngine->page()->runJavaScript(Translate::toastScript(msg));
  };
  if (!Translate::isEnabled()) {
    toast(tr("Inline translation is off (enable it in Settings → Translation)."));
    return;
  }
  if (trimmed.isEmpty()) {
    toast(intoComposer ? tr("The message box is empty.")
                       : tr("Select some text to translate first."));
    return;
  }
  if (!m_translator)
    m_translator = new Translator(this);

  // One request at a time: connect fresh handlers and tear both down when
  // either fires, so results never cross wires between calls.
  auto conns = std::make_shared<QList<QMetaObject::Connection>>();
  auto cleanup = [conns]() {
    for (const auto &c : *conns)
      QObject::disconnect(c);
    conns->clear();
  };
  conns->append(connect(
      m_translator, &Translator::translated, this,
      [this, intoComposer, toast, cleanup](const QString &out, const QString &) {
        cleanup();
        if (intoComposer) {
          if (m_webEngine && m_webEngine->page())
            m_webEngine->page()->runJavaScript(
                Translate::replaceComposerScript(out));
        } else {
          toast(out);
        }
      }));
  conns->append(connect(m_translator, &Translator::failed, this,
                        [toast, cleanup](const QString &err) {
                          cleanup();
                          toast(err);
                        }));
  m_translator->translate(trimmed, appTargetLanguage());
}

// ── Export the open conversation (idea #9) ──────────────────────────────────

void MainWindow::exportCurrentChat() {
  if (!m_webEngine || !m_webEngine->page()) {
    showNotification(tr("Export chat"), tr("No conversation is open."));
    return;
  }
  if (m_exportPollTimer && m_exportPollTimer->isActive()) {
    showNotification(tr("Export chat"), tr("An export is already running."));
    return;
  }

  const QString baseDir = QFileDialog::getExistingDirectory(
      this, tr("Choose a folder for the exported chat"),
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  if (baseDir.isEmpty())
    return;

  auto *progress = new QProgressDialog(
      tr("Collecting messages… scrolling through the conversation."),
      tr("Cancel"), 0, 0, this);
  progress->setWindowTitle(tr("Export chat"));
  progress->setWindowModality(Qt::NonModal);
  progress->setMinimumDuration(0);
  progress->setValue(0);
  progress->show();

  // Kick the async page collector; it stores its result on window.__whatlyExport.
  m_webEngine->page()->runJavaScript(ChatExport::collectorScript());

  if (!m_exportPollTimer)
    m_exportPollTimer = new QTimer(this);
  m_exportPollTimer->setInterval(600);

  QWidget *pageOwner = m_webEngine;
  auto finish = [this, progress]() {
    if (m_exportPollTimer)
      m_exportPollTimer->stop();
    if (m_exportPollTimer)
      m_exportPollTimer->disconnect();
    progress->close();
    progress->deleteLater();
  };

  connect(progress, &QProgressDialog::canceled, this, [finish, this]() {
    finish();
    // Best-effort: let the page abandon the collector on the next reload.
    if (m_webEngine && m_webEngine->page())
      m_webEngine->page()->runJavaScript(
          QStringLiteral("window.__whatlyExport={status:'cancelled'};"));
  });

  connect(m_exportPollTimer, &QTimer::timeout, this,
          [this, progress, baseDir, finish, pageOwner]() {
            if (!m_webEngine || m_webEngine != pageOwner || !m_webEngine->page()) {
              finish();
              return;
            }
            m_webEngine->page()->runJavaScript(
                ChatExport::statusScript(), [this, progress, baseDir, finish](
                                                const QVariant &v) {
                  const QJsonObject st =
                      QJsonDocument::fromJson(v.toString().toUtf8()).object();
                  const QString status = st.value("status").toString();
                  if (status == QLatin1String("running")) {
                    const int count = st.value("count").toInt();
                    progress->setLabelText(
                        tr("Collecting messages… (%1 so far)").arg(count));
                    return;
                  }
                  if (status == QLatin1String("error")) {
                    finish();
                    showNotification(tr("Export chat"),
                                     tr("Could not read the conversation: %1")
                                         .arg(st.value("error").toString()));
                    return;
                  }
                  if (status != QLatin1String("done"))
                    return; // "none"/"cancelled": stop quietly next tick
                  // Pull the full payload once, then write it out.
                  finish();
                  writeChatExport(baseDir);
                });
          });
  m_exportPollTimer->start();
}

// Reads window.__whatlyExport (the completed payload) and writes the files.
void MainWindow::writeChatExport(const QString &baseDir) {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      QStringLiteral("JSON.stringify(window.__whatlyExport||{})"),
      [this, baseDir](const QVariant &v) {
        const QJsonObject root =
            QJsonDocument::fromJson(v.toString().toUtf8()).object();
        const QString chatName = root.value("chat").toString(tr("chat"));
        const QJsonArray raw = root.value("messages").toArray();

        QHash<QString, QByteArray> media;
        const QList<ChatExport::Message> msgs = ChatExport::parse(raw, &media);

        const QString folderName = QStringLiteral("%1 - %2").arg(
            ChatExport::sanitizeFileName(chatName),
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));
        QDir dir(baseDir);
        if (!dir.mkpath(folderName)) {
          showNotification(tr("Export chat"),
                           tr("Could not create the export folder."));
          return;
        }
        dir.cd(folderName);

        bool ok = true;
        QFile txt(dir.filePath(QStringLiteral("chat.txt")));
        if (txt.open(QIODevice::WriteOnly | QIODevice::Text))
          txt.write(ChatExport::buildTranscript(chatName, msgs).toUtf8());
        else
          ok = false;
        QFile js(dir.filePath(QStringLiteral("chat.json")));
        if (js.open(QIODevice::WriteOnly))
          js.write(ChatExport::buildJson(msgs));
        else
          ok = false;

        int savedMedia = 0;
        if (!media.isEmpty() && dir.mkpath(QStringLiteral("media"))) {
          for (auto it = media.constBegin(); it != media.constEnd(); ++it) {
            QFile f(dir.filePath(QStringLiteral("media/") + it.key()));
            if (f.open(QIODevice::WriteOnly)) {
              f.write(it.value());
              ++savedMedia;
            }
          }
        }

        if (!ok) {
          showNotification(tr("Export chat"),
                           tr("The export could not be fully written."));
          return;
        }
        showNotification(tr("Export chat"),
                         tr("Saved %1 messages and %2 media files to %3")
                             .arg(msgs.size())
                             .arg(savedMedia)
                             .arg(dir.absolutePath()));
      });
}

// ── Inline-reply notifications (idea #2) ────────────────────────────────────

bool MainWindow::ensureInlineReply() {
  if (!NotificationRules::inlineReplyEnabled())
    return false;
  if (!m_notificationReply) {
    m_notificationReply = new NotificationReply(this);

    // A typed reply: send it straight to the chat the notification was for,
    // through the existing name-sender automation (opens the chat by exact
    // title and sends; no window navigation needed).
    connect(m_notificationReply, &NotificationReply::replied, this,
            [this](quint32 id, const QString &text) {
              auto it = m_replyNotifs.find(id);
              if (it == m_replyNotifs.end())
                return;
              const QString chat = it->second;
              m_replyNotifs.erase(it);
              const QString body = text.trimmed();
              if (body.isEmpty())
                return;
              // App Lock gate (issue #41): a locked app must not send an
              // inline-reply from the notification. (This path calls
              // sendByNameViaWeb directly, not commandSend.)
              if (m_lockWidget && m_lockWidget->getIsLocked()) {
                showNotification(QApplication::applicationDisplayName(),
                                 tr("Whatly is locked. Unlock it to send "
                                    "messages."));
                return;
              }
              Messaging::Recipient r;
              r.kind = Messaging::RecipientKind::ContactName;
              r.value = chat;
              r.raw = chat;
              sendByNameViaWeb(r, body, QString());
            });

    // A plain click still opens the conversation, like the other backends.
    connect(m_notificationReply, &NotificationReply::actionInvoked, this,
            [this](quint32 id, const QString &action) {
              auto it = m_replyNotifs.find(id);
              if (it == m_replyNotifs.end())
                return;
              if (action != QLatin1String("default") &&
                  action != QLatin1String("open"))
                return;
              WebEngineNotifProxyPtr proxy = it->first;
              m_replyNotifs.erase(it);
              if (proxy)
                proxy->invoke(&QWebEngineNotification::click);
              notificationClicked();
            });

    connect(m_notificationReply, &NotificationReply::closed, this,
            [this](quint32 id, quint32) {
              auto it = m_replyNotifs.find(id);
              if (it == m_replyNotifs.end())
                return;
              WebEngineNotifProxyPtr proxy = it->first;
              m_replyNotifs.erase(it);
              if (proxy)
                proxy->invoke(&QWebEngineNotification::close);
            });
  }
  return m_notificationReply->isAvailable();
}

// ── AI assistant (idea #5) ──────────────────────────────────────────────────

void MainWindow::runAssistant(const QString &systemPrompt,
                              const QString &userPrompt,
                              std::function<void(const QString &)> onResult) {
  auto toast = [this](const QString &msg, bool persistent) {
    if (m_webEngine && m_webEngine->page())
      m_webEngine->page()->runJavaScript(
          Translate::toastScript(msg, persistent));
  };
  auto hideToast = [this]() {
    if (m_webEngine && m_webEngine->page())
      m_webEngine->page()->runJavaScript(Translate::hideToastScript());
  };
  if (!Ai::isEnabled()) {
    toast(tr("The AI assistant is off (enable it in Settings → AI assistant)."),
          false);
    return;
  }
  if (userPrompt.trimmed().isEmpty()) {
    toast(tr("There is nothing for the assistant to work on."), false);
    return;
  }
  if (!m_aiClient)
    m_aiClient = new AiClient(this);

  // A persistent toast while the model works (it can take a while, especially a
  // local one), removed as soon as the result or an error arrives. When free
  // memory is low, warn: a local model loading several GB can exhaust RAM and
  // destabilise the page (as opposed to a remote endpoint, which does not).
  const long freeMb = Ai::availableMemoryMb();
  if (freeMb >= 0 && freeMb < 1500)
    toast(tr("Asking the assistant… (low memory: %1 MB free; a local model "
             "may fail or slow the app)")
              .arg(freeMb),
          true);
  else
    toast(tr("Asking the assistant…"), true);

  auto conns = std::make_shared<QList<QMetaObject::Connection>>();
  auto cleanup = [conns]() {
    for (const auto &c : *conns)
      QObject::disconnect(c);
    conns->clear();
  };
  conns->append(connect(m_aiClient, &AiClient::completed, this,
                        [onResult, cleanup, hideToast](const QString &text) {
                          cleanup();
                          hideToast();
                          onResult(text);
                        }));
  conns->append(connect(m_aiClient, &AiClient::failed, this,
                        [this, toast, cleanup](const QString &err) {
                          cleanup();
                          toast(err, false);
                          // Also a desktop notification: the in-page toast is
                          // easy to miss (or gone) if the window lost focus
                          // during a slow request.
                          showNotification(tr("AI assistant"), err);
                        }));
  m_aiClient->complete(systemPrompt, userPrompt);
}

void MainWindow::showTextDialog(const QString &title, const QString &text) {
  auto *dlg = new QDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle(title);
  dlg->resize(520, 380);
  auto *lay = new QVBoxLayout(dlg);
  auto *edit = new QPlainTextEdit(dlg);
  edit->setPlainText(text);
  edit->setReadOnly(true);
  lay->addWidget(edit);
  auto *close = new QPushButton(tr("Close"), dlg);
  connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
  lay->addWidget(close);
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

void MainWindow::deliverAiText(const QString &text) {
  if (!m_webEngine || !m_webEngine->page()) {
    showTextDialog(tr("AI result"), text);
    return;
  }

  // Deliver exactly once, and never depend on the page staying responsive: a
  // heavy local model can leave the render process briefly unresponsive, and a
  // page callback that never fires would otherwise swallow the result. A timer
  // guarantees the result is shown (in a dialog) if we cannot confirm within a
  // short window that it landed in the composer.
  auto done = std::make_shared<bool>(false);
  QTimer::singleShot(3000, this, [this, text, done]() {
    if (*done)
      return;
    *done = true;
    showTextDialog(tr("AI result"), text);
  });

  auto *page = m_webEngine->page();
  page->runJavaScript(
      Translate::readComposerScript(), [this, text, done](const QVariant &b) {
        if (*done || !m_webEngine || !m_webEngine->page())
          return;
        const QString before = b.toString().trimmed();
        m_webEngine->page()->runJavaScript(Translate::replaceComposerScript(text));
        m_webEngine->page()->runJavaScript(
            Translate::readComposerScript(),
            [this, text, before, done](const QVariant &a) {
              if (*done)
                return;
              *done = true;
              const QString after = a.toString().trimmed();
              if (!after.isEmpty() && after != before) {
                if (m_webEngine && m_webEngine->page())
                  m_webEngine->page()->runJavaScript(
                      Translate::toastScript(tr("Message updated."), false));
              } else {
                // Composer did not change (e.g. window unfocused) — show it.
                showTextDialog(tr("AI result"), text);
              }
            });
      });
}

void MainWindow::aiSummarizeChat() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      Ai::readContextScript(200), [this](const QVariant &v) {
        runAssistant(Ai::summarizeSystemPrompt(), v.toString(),
                     [this](const QString &summary) {
                       showTextDialog(tr("Chat summary"), summary);
                     });
      });
}

void MainWindow::aiImproveComposer() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      Translate::readComposerScript(), [this](const QVariant &v) {
        runAssistant(Ai::improveSystemPrompt(), v.toString(),
                     [this](const QString &improved) { deliverAiText(improved); });
      });
}

void MainWindow::aiSuggestReply() {
  if (!m_webEngine || !m_webEngine->page())
    return;
  m_webEngine->page()->runJavaScript(
      Ai::readContextScript(60), [this](const QVariant &v) {
        runAssistant(Ai::suggestReplySystemPrompt(), v.toString(),
                     [this](const QString &suggestion) {
                       deliverAiText(suggestion);
                     });
      });
}
