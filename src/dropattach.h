#ifndef DROPATTACH_H
#define DROPATTACH_H

#include <QList>
#include <QString>

// Builds the JavaScript that hands files dropped onto the window to WhatsApp
// Web as an attachment. Qt WebEngine does not forward an OS file drop to the
// page, so WebView catches the drop, reads the files, and injects this script,
// which rebuilds them as File objects in a DataTransfer and dispatches a
// synthetic drop event on the open conversation (issue #285). Kept pure so the
// generator is unit-tested without a live page.
namespace DropAttach {

struct File {
  QString name;   // original file name shown in the composer
  QString mime;   // MIME type (e.g. image/png, application/pdf)
  QString base64; // file contents, base64-encoded
};

// The injected script, or an empty string when there are no files.
QString scriptSource(const QList<File> &files);

} // namespace DropAttach

#endif // DROPATTACH_H
