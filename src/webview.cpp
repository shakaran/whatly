#include "webview.h"

#include <QBuffer>
#include <QChildEvent>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QUrl>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif
#include <mainwindow.h>
#include <QWebEngineContextMenuRequest>

#include "dropattach.h"
#include "settingsmanager.h"

using QWebEngineContextMenuData = QWebEngineContextMenuRequest;

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent) {

  // Accept dropped files so they can be sent as attachments (issue #285).
  setAcceptDrops(true);

  QObject *parentMainWindow = this->parent();
  while (!parentMainWindow->objectName().contains("MainWindow")) {
    parentMainWindow = parentMainWindow->parent();
  }
  MainWindow *mainWindow = dynamic_cast<MainWindow *>(parentMainWindow);

  connect(this, &WebView::titleChanged, mainWindow,
          &MainWindow::handleWebViewTitleChanged);
  connect(this, &WebView::loadFinished, mainWindow,
          &MainWindow::handleLoadFinished);
  connect(this, &WebView::renderProcessTerminated,
          [this](QWebEnginePage::RenderProcessTerminationStatus termStatus,
                 int statusCode) {
            QString status;
            switch (termStatus) {
            case QWebEnginePage::NormalTerminationStatus:
              status = tr("Render process normal exit");
              break;
            case QWebEnginePage::AbnormalTerminationStatus:
              status = tr("Render process abnormal exit");
              break;
            case QWebEnginePage::CrashedTerminationStatus:
              status = tr("Render process crashed");
              break;
            case QWebEnginePage::KilledTerminationStatus:
              status = tr("Render process killed");
              break;
            }
            // A normal exit is not a crash (it happens on a deliberate reload),
            // so never act on it. For the crash/kill/abnormal cases, honour the
            // "reload automatically after a crash" setting: reload without
            // interrupting the user, otherwise ask as before (issue #225).
            if (termStatus == QWebEnginePage::NormalTerminationStatus)
              return;

            const bool autoRestart = SettingsManager::instance()
                                         .settings()
                                         .value("autoRestartOnCrash", false)
                                         .toBool();
            if (autoRestart) {
              qWarning() << "Render process ended:" << status
                         << "code" << statusCode << "- reloading automatically.";
              QTimer::singleShot(0, this, [this] { this->reload(); });
              return;
            }

            QMessageBox::StandardButton btn =
                QMessageBox::question(window(), status,
                                      tr("Render process exited with code: %1\n"
                                         "Do you want to reload the page ?")
                                          .arg(statusCode));
            if (btn == QMessageBox::Yes)
              QTimer::singleShot(0, this, [this] { this->reload(); });
          });
}

void WebView::wheelEvent(QWheelEvent *event) {
  bool controlKeyIsHeld =
      QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
  // this doesn't work, (even after checking the global QApplication keyboard
  // modifiers) as expected, the Ctrl+wheel is managed by Chromium
  // WebenginePage directly. So, we manage it by injecting js to page using
  // WebEnginePage::injectPreventScrollWheelZoomHelper
  if ((event->modifiers() & Qt::ControlModifier) != 0 || controlKeyIsHeld) {
    qDebug() << "skipped ctrl + m_wheel event on webengineview";
    event->ignore();
  } else {
    QWebEngineView::wheelEvent(event);
  }
}

void WebView::contextMenuEvent(QContextMenuEvent *event) {
  auto menu = createStandardContextMenu();
  menu->setAttribute(Qt::WA_DeleteOnClose, true);
  // hide reload, back, forward, savepage, copyimagelink menus
  foreach (auto *action, menu->actions()) {
    if (action == page()->action(QWebEnginePage::SavePage) ||
        action == page()->action(QWebEnginePage::Reload) ||
        action == page()->action(QWebEnginePage::Back) ||
        action == page()->action(QWebEnginePage::Forward) ||
        action == page()->action(QWebEnginePage::CopyImageUrlToClipboard)) {
      action->setVisible(false);
    }
  }

  const QWebEngineContextMenuRequest &data = *lastContextMenuRequest();

  // allow context menu on image
  if (data.mediaType() == QWebEngineContextMenuData::MediaTypeImage) {
    QWebEngineView::contextMenuEvent(event);
    return;
  }
  // if content is not editable
  if (data.selectedText().isEmpty() && !data.isContentEditable()) {
    event->ignore();
    return;
  }

  connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
  menu->popup(event->globalPos());
}

// ── Clipboard image paste ─────────────────────────────────────────────────────
//
// Qt WebEngine drops the image when the clipboard also carries url/html
// flavours — which is exactly what a browser puts there on "Copy image". The
// page then only receives a text/uri-list and nothing is pasted. A clipboard
// holding just an image (a screenshot tool) arrives correctly as a File, so
// only the mixed case needs rescuing: read the image with QClipboard, which
// does see it, and hand it to the page as a File in a synthetic paste event.

// QWebEngineView delivers input through an internal child widget, so the key
// press has to be caught there rather than on the view itself.
bool WebView::event(QEvent *event) {
  if (event->type() == QEvent::ChildAdded) {
    auto *childEvent = static_cast<QChildEvent *>(event);
    if (childEvent->child() && childEvent->child()->isWidgetType())
      childEvent->child()->installEventFilter(this);
  }
  return QWebEngineView::event(event);
}

bool WebView::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->matches(QKeySequence::Paste) && pasteClipboardImage())
      return true; // handled here; skip the native paste that would drop it
  }

  // Drops arrive on the internal render widget, not the view. Only claim drops
  // that carry files (issue #285); leave in-page drags to Chromium. A file
  // dragged into a Flatpak sandbox comes as the XDG FileTransfer portal mime
  // rather than a local URL (issue #32), so accept that too.
  if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
    auto *dragEvent = static_cast<QDragMoveEvent *>(event);
    const QMimeData *mime = dragEvent->mimeData();
    bool droppable = mime &&
        mime->hasFormat(QStringLiteral("application/vnd.portal.filetransfer"));
    if (mime && !droppable && mime->hasUrls()) {
      for (const QUrl &url : mime->urls())
        if (url.isLocalFile()) { droppable = true; break; }
    }
    if (droppable) {
      dragEvent->acceptProposedAction();
      return true;
    }
  } else if (event->type() == QEvent::Drop) {
    auto *dropEvent = static_cast<QDropEvent *>(event);
    if (dropFiles(dropEvent->mimeData())) {
      dropEvent->acceptProposedAction();
      return true;
    }
  }
  return QWebEngineView::eventFilter(watched, event);
}

// Turn a drop's contents into paths this process can actually read. Native
// builds get the plain local paths from the URLs. Inside a Flatpak sandbox an
// OS drag arrives via the XDG FileTransfer portal (mime
// application/vnd.portal.filetransfer), and the host paths in the URLs are not
// visible here; the portal's RetrieveFiles hands back readable paths under
// /run/user/<uid>/doc/ instead (issue #32). Falls back to the URLs whenever the
// portal path is absent or yields nothing.
static QStringList resolveDroppedFilePaths(const QMimeData *mime) {
  QStringList paths;
#ifdef Q_OS_LINUX
  const QString kPortalMime =
      QStringLiteral("application/vnd.portal.filetransfer");
  if (mime->hasFormat(kPortalMime)) {
    QString key = QString::fromUtf8(mime->data(kPortalMime));
    key.remove(QChar(u'\0'));
    key = key.trimmed();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileTransfer"),
                          QDBusConnection::sessionBus());
    if (portal.isValid() && !key.isEmpty()) {
      const QDBusReply<QStringList> reply =
          portal.call(QStringLiteral("RetrieveFiles"), key, QVariantMap());
      if (reply.isValid())
        paths = reply.value();
      else
        qWarning() << "whatly: FileTransfer portal RetrieveFiles failed:"
                   << reply.error().message();
    }
  }
#endif
  if (paths.isEmpty()) {
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls)
      if (url.isLocalFile())
        paths << url.toLocalFile();
  }
  return paths;
}

bool WebView::dropFiles(const QMimeData *mime) {
  if (!mime)
    return false;

  const QStringList paths = resolveDroppedFilePaths(mime);

  // Cap the total read into memory: the files are base64-encoded and handed to
  // the page as a JS string, so a huge drop would balloon the renderer.
  constexpr qint64 kMaxTotalBytes = 64LL * 1024 * 1024;

  QList<DropAttach::File> files;
  QMimeDatabase mimeDb;
  qint64 total = 0;
  for (const QString &path : paths) {
    const QFileInfo info(path);
    if (!info.isFile())
      continue;
    if (info.size() <= 0 || total + info.size() > kMaxTotalBytes) {
      qWarning() << "whatly: skipping dropped file (too large for the drop"
                    " buffer):"
                 << info.fileName();
      continue;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
      continue;
    const QByteArray data = file.readAll();
    total += data.size();
    const QString type =
        mimeDb.mimeTypeForFileNameAndData(info.fileName(), data).name();
    files.append({info.fileName(), type, QString::fromLatin1(data.toBase64())});
  }

  if (files.isEmpty()) {
    // Fail loudly: the drag was accepted but nothing could be read. In a Flatpak
    // this is the sandbox hiding the host path when the portal route did not
    // apply (issue #32); elsewhere it is an unreadable or empty file.
    if (mime->hasUrls() ||
        mime->hasFormat(QStringLiteral("application/vnd.portal.filetransfer")))
      qWarning() << "whatly: a file was dropped but none could be read; in a "
                    "Flatpak, files outside the granted folders are not visible "
                    "unless delivered through the FileTransfer portal (issue #32)";
    return false;
  }

  page()->runJavaScript(DropAttach::scriptSource(files));
  return true;
}

bool WebView::pasteClipboardImage() {
  const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
  if (!mimeData || !mimeData->hasImage())
    return false; // no image at all: nothing to rescue

  // An image-only clipboard already pastes correctly; leave that path alone.
  if (!mimeData->hasUrls() && !mimeData->hasHtml())
    return false;

  const QImage image = qvariant_cast<QImage>(mimeData->imageData());
  if (image.isNull())
    return false;

  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    return false;
  buffer.close();

  static const QString kInject = QStringLiteral(R"JS(
(function () {
  try {
    var binary = atob("%1");
    var bytes = new Uint8Array(binary.length);
    for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    var file = new File([bytes], "image.png", { type: "image/png" });
    var transfer = new DataTransfer();
    transfer.items.add(file);
    var target = document.activeElement || document.body;
    target.dispatchEvent(new ClipboardEvent("paste", {
      clipboardData: transfer, bubbles: true, cancelable: true
    }));
  } catch (e) {
    console.error("whatly: pasting the clipboard image failed: " + e);
  }
})();
)JS");

  page()->runJavaScript(kInject.arg(QString::fromLatin1(png.toBase64())));
  return true;
}
