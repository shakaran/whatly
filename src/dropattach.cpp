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
    var box = document.querySelector("#main footer [contenteditable='true']") ||
              document.querySelector("[contenteditable='true']");
    if (box) box.focus();
    var target = box || document.activeElement || document.body;
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
