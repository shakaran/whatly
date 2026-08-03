#include "dropattach.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace DropAttach {

QString scriptSource(const QList<File> &files) {
  if (files.isEmpty())
    return QString();

  QJsonArray arr;
  for (const File &f : files) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), f.name);
    o.insert(QStringLiteral("type"), f.mime);
    o.insert(QStringLiteral("b64"), f.base64);
    arr.append(o);
  }
  const QString json =
      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

  // Rebuild each file as a File in a DataTransfer and hand it to the open chat
  // with a synthetic paste event on the message composer. A synthetic HTML5
  // drop event does not reach WhatsApp Web's handler, but a paste does, and it
  // opens the right preview for every type (media editor for images/videos, the
  // document preview otherwise) exactly as pasting or a real drop would. The
  // composer is focused first so the paste lands on the active conversation.
  static const QString kTemplate = QStringLiteral(R"JS(
(function () {
  try {
    var files = %1;
    var dt = new DataTransfer();
    files.forEach(function (f) {
      var binary = atob(f.b64);
      var bytes = new Uint8Array(binary.length);
      for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
      dt.items.add(new File([bytes], f.name, { type: f.type }));
    });
    // The paste only lands on a box that has the focus, exactly as when you click
    // into a caption before pressing Ctrl+V, so whichever box is chosen has to be
    // focused here.
    //
    // Which box: with no attachment open there is just the chat composer, in the
    // footer. Opening the media preview adds its caption box OUTSIDE the footer
    // and does NOT hide the composer, which stays visible and even keeps the
    // focus — so preferring the composer sent every extra file to the chat that
    // was not accepting it, and the file vanished. A visible editable box outside
    // the footer therefore means the preview is open and is the one to paste into.
    var visible = function (el) { return el && el.offsetParent !== null; };
    var inFooter = function (el) {
      return !!(el && el.closest && el.closest("#main footer"));
    };
    var boxes = document.querySelectorAll("[contenteditable='true']");
    var caption = null, composer = null;
    for (var b = 0; b < boxes.length; b++) {
      if (!visible(boxes[b]))
        continue;
      if (inFooter(boxes[b])) {
        if (!composer) composer = boxes[b];
      } else if (!caption) {
        caption = boxes[b];
      }
    }
    var target = caption || composer || document.activeElement || document.body;
    if (target.focus) target.focus();
    target.dispatchEvent(new ClipboardEvent("paste", {
      clipboardData: dt, bubbles: true, cancelable: true
    }));
  } catch (e) {
    console.error("whatly: dropping the attachment failed: " + e);
  }
})();
)JS");

  return kTemplate.arg(json);
}

} // namespace DropAttach
