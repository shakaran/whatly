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

  // Rebuild each file as a File in a DataTransfer, then fire the drag sequence
  // (dragenter, dragover, drop) on the open conversation so WhatsApp Web opens
  // its media preview exactly as it would for a real drop. #main is the chat
  // panel; fall back to the body when no chat is open.
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
    var target = document.querySelector("#main") || document.body;
    ["dragenter", "dragover", "drop"].forEach(function (type) {
      target.dispatchEvent(new DragEvent(type, {
        bubbles: true, cancelable: true, dataTransfer: dt
      }));
    });
  } catch (e) {
    console.error("whatly: dropping the attachment failed: " + e);
  }
})();
)JS");

  return kTemplate.arg(json);
}

} // namespace DropAttach
