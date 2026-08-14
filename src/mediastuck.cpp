#include "mediastuck.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

namespace MediaStuck {

bool isReport(const QString &consoleMessage) {
  return consoleMessage.startsWith(kMarker);
}

Report parse(const QString &consoleMessage) {
  Report r;
  if (!isReport(consoleMessage))
    return r;
  const QJsonObject o =
      QJsonDocument::fromJson(
          consoleMessage.mid(QString(kMarker).size()).toUtf8())
          .object();
  r.attempts = o.value(QStringLiteral("attempts")).toInt();
  // A missing flag means the page said nothing about the connection; assume it
  // was there, so the advice does not blame the network without grounds.
  r.online = o.value(QStringLiteral("online")).toBool(true);
  return r;
}

Advice adviceFor(const Report &report) {
  // One attempt is not a failure yet: WhatsApp fetches media lazily and the
  // first click often simply takes a moment.
  if (report.attempts < 2)
    return Advice::None;
  return report.online ? Advice::Expired : Advice::Offline;
}

QString text(Advice advice) {
  switch (advice) {
  case Advice::Offline:
    return QCoreApplication::translate(
        "MediaStuck", "This file did not download: there is no connection at "
                      "the moment. It will arrive on its own once there is. "
                      "(Click to dismiss.)");
  case Advice::Expired:
    return QCoreApplication::translate(
        "MediaStuck",
        "This file did not download. WhatsApp keeps a copy on its servers only "
        "for a while, and it is usually gone by the time a message is old — "
        "there is nothing here that can fetch it back. Ask for it again from "
        "the phone that sent it. (Click to dismiss.)");
  case Advice::None:
    break;
  }
  return QString();
}

QString watcherScript() {
  // Runs in the page. It knows nothing about why a download fails — only that
  // one was asked for twice and the bubble is still showing its size and its
  // arrow after a wait, which is what a download that never came looks like.
  return QStringLiteral(R"JS(
(function () {
  'use strict';
  if (window.__whatlyMediaWatch) return;
  window.__whatlyMediaWatch = true;

  var GRACE  = 6000;    // how long the file has to arrive after the click
  var FORGET = 120000;  // clicks further apart than this are not the same try
  var MUTE   = 60000;   // having said it once, keep quiet about this one
  var asked  = new Map();

  // A bubble waiting for its media shows a size, and nothing that has arrived.
  // The blurred placeholder is a data: URI, so it does not count as arrived.
  function stillWaiting(row) {
    if (!row || !row.isConnected) return false;
    if (row.querySelector('video, audio, img[src^="blob:"], img[src^="http"]'))
      return false;
    return /\d+([.,]\d+)?\s?(kB|KB|MB|GB)/.test(row.textContent || '');
  }

  document.addEventListener('click', function (e) {
    var target = e.target;
    if (!target || !target.closest) return;
    // Any click inside a bubble that is still waiting counts as asking for it.
    // Keying on the download button alone missed the ask: the placeholder
    // itself starts the download too, so the first tries went uncounted and the
    // notice arrived a click or two later than it should have.
    var row = target.closest('[data-id]');
    if (!stillWaiting(row)) return;

    var id = row.getAttribute('data-id') || '';
    var now = Date.now();
    var rec = asked.get(id);
    if (!rec || now - rec.when > FORGET) rec = {times: 0, when: now, told: 0};
    rec.times += 1;
    rec.when = now;
    asked.set(id, rec);
    if (rec.times < 2) return;                        // the first ask in silence
    if (rec.told && now - rec.told < MUTE) return;    // already said, once is enough

    var times = rec.times;
    setTimeout(function () {
      if (!stillWaiting(row)) return;   // it came after all: say nothing
      // Impatience is normal, and every further click had armed its own wait.
      // Without this the notice was printed several times over, and each one
      // replaced the last — which read as the notice jumping about the page.
      var current = asked.get(id);
      if (current && current.told && Date.now() - current.told < MUTE) return;
      if (current) current.told = Date.now();
      console.log('WHATLY_MEDIA_STUCK ' + JSON.stringify({
        attempts: times,
        online: navigator.onLine !== false
      }));
    }, GRACE);
  }, true);
})();
)JS");
}

} // namespace MediaStuck
