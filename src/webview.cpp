#include "webview.h"
#include "dropprogress.h"
#include "dropreader.h"
#include "dropresolve.h"

#include <QBuffer>
#include <QChildEvent>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
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
#include <QThread>
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

            // Break a runaway crash loop (issue #28): if the renderer keeps
            // terminating in a short window — seen in the Flatpak where it is
            // repeatedly SIGTERM'd (code 15) — neither auto-reloading nor
            // re-prompting helps; both just relaunch a renderer that dies again,
            // stacking endless dialogs. Count terminations within a 60s window
            // and, past a small threshold, stop and surface the problem once.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastRenderCrashMs == 0 || now - m_lastRenderCrashMs > 60000)
              m_renderCrashCount = 0;
            m_lastRenderCrashMs = now;
            ++m_renderCrashCount;
            constexpr int kCrashLoopThreshold = 3;

            const bool autoRestart = SettingsManager::instance()
                                         .settings()
                                         .value("autoRestartOnCrash", false)
                                         .toBool();

            if (m_renderCrashCount >= kCrashLoopThreshold) {
              qWarning() << "Render process ended:" << status << "code"
                         << statusCode << "-" << m_renderCrashCount
                         << "times in a row; stopping the reload loop.";
              // Only one notice, and not another until the user acts.
              if (!m_renderCrashDialogUp) {
                m_renderCrashDialogUp = true;
                QMessageBox::warning(
                    window(), tr("WhatsApp Web keeps closing"),
                    tr("WhatsApp Web's renderer keeps terminating (code %1), so "
                       "Whatly has stopped reloading it to avoid a loop.\n\n"
                       "This is often a GPU or sandbox problem. Try turning off "
                       "GPU acceleration in Settings → Performance, then reload.")
                        .arg(statusCode));
                m_renderCrashDialogUp = false;
                // Let a later, calmer crash prompt again from scratch.
                m_renderCrashCount = 0;
                m_lastRenderCrashMs = 0;
              }
              return;
            }

            if (autoRestart) {
              qWarning() << "Render process ended:" << status
                         << "code" << statusCode << "- reloading automatically.";
              QTimer::singleShot(0, this, [this] { this->reload(); });
              return;
            }

            // Don't stack modal dialogs if terminations arrive back to back.
            if (m_renderCrashDialogUp)
              return;
            m_renderCrashDialogUp = true;
            QMessageBox::StandardButton btn =
                QMessageBox::question(window(), status,
                                      tr("Render process exited with code: %1\n"
                                         "Do you want to reload the page ?")
                                          .arg(statusCode));
            m_renderCrashDialogUp = false;
            if (btn == QMessageBox::Yes)
              QTimer::singleShot(0, this, [this] { this->reload(); });
          });
}

WebView::~WebView() {
  // A drop read may still be running on the worker thread, which is parented to
  // this view — destroying the view would otherwise destroy a running QThread
  // and crash. Ask the reader to stop (cancel() makes the wait return promptly
  // rather than blocking on a full read), then wait for the thread to end.
  if (m_dropReader)
    m_dropReader->cancel();
  if (m_dropThread) {
    m_dropThread->quit();
    m_dropThread->wait();
  }
  // The progress bar is parented to the top-level window, not to this view, so
  // it would outlive the view with a dangling host unless removed here.
  delete m_dropProgress;
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

  const bool editable = data.isContentEditable();
  const bool hasSelection = !data.selectedText().isEmpty();

  // Whatly's own actions (AI, translate, export), grouped by where they apply.
  // This is the discoverable path to them: no shortcut or command palette
  // needed. Composer actions show in the message box, selection actions when
  // text is selected, chat actions always.
  auto addGroup = [&](const QList<QAction *> &acts) {
    if (acts.isEmpty())
      return;
    menu->addSeparator();
    for (QAction *a : acts)
      menu->addAction(a);
  };
  if (editable)
    addGroup(m_composerActions);
  if (hasSelection)
    addGroup(m_selectionActions);
  addGroup(m_chatActions);

  // Nothing useful to show on a plain, non-editable area with no selection and
  // no app actions: keep the previous "no menu" behaviour.
  if (!editable && !hasSelection && m_chatActions.isEmpty()) {
    event->ignore();
    return;
  }

  connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
  menu->popup(event->globalPos());
}

void WebView::setContextActions(const QList<QAction *> &composer,
                                const QList<QAction *> &selection,
                                const QList<QAction *> &chat) {
  m_composerActions = composer;
  m_selectionActions = selection;
  m_chatActions = chat;
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
bool WebView::dropFiles(const QMimeData *mime) {
  if (!mime)
    return false;

  const QStringList paths = DropResolve::droppedFilePaths(mime);

  // A drop while one is still being read is queued rather than refused: the
  // page's composer takes a second paste as another attachment, so the files
  // join the ones already there instead of replacing them.
  if (m_dropReading && !paths.isEmpty()) {
    m_queuedDropPaths += paths;
    return true;
  }

  return startDropRead(paths, mime);
}

QString WebView::takeTooLargeMessage() {
  if (m_dropTooLarge.isEmpty())
    return QString();
  const QString names = m_dropTooLarge.join(QStringLiteral(", "));
  m_dropTooLarge.clear();
  return tr("Too large to attach (limit %1 MB): %2")
      .arg(DropReader::kMaxTotalBytes / (1024 * 1024))
      .arg(names);
}

bool WebView::startDropRead(const QStringList &paths, const QMimeData *mime) {
  const DropReader::Plan plan = DropReader::plan(paths);

  for (const QString &name : plan.tooLarge)
    qWarning() << "whatly: skipping dropped file (too large for the drop"
                  " buffer):"
               << name;
  // Collected across the whole run of drops, not just this batch: with a queued
  // batch still to read, reporting now would be overwritten by its progress.
  m_dropTooLarge += plan.tooLarge;

  if (plan.accepted.isEmpty()) {
    // Everything was over the cap: say so on screen rather than only in a
    // terminal nobody sees.
    if (!m_dropTooLarge.isEmpty()) {
      if (!m_dropProgress)
        m_dropProgress = new DropProgress(this);
      m_dropProgress->finish(takeTooLargeMessage());
      return false;
    }
    // Fail loudly: the drag was accepted but nothing could be read. In a Flatpak
    // this is the sandbox hiding the host path when the portal route did not
    // apply (issue #32); elsewhere it is an unreadable or empty file. Only for a
    // real drop; a queued batch has no mime data to inspect.
    if (mime && (mime->hasUrls() ||
                 mime->hasFormat(
                     QStringLiteral("application/vnd.portal.filetransfer"))))
      qWarning() << "whatly: a file was dropped but none could be read; in a "
                    "Flatpak, files outside the granted folders are not visible "
                    "unless delivered through the FileTransfer portal (issue #32)";
    return false;
  }

  if (!m_dropProgress)
    m_dropProgress = new DropProgress(this);
  m_dropProgress->begin();
  m_dropReading = true;

  // Read and encode on a worker thread: for a video this is seconds of work,
  // and on the UI thread it froze the window with nothing to show for it.
  auto *thread = new QThread(this);
  auto *reader = new DropReader(plan.accepted, plan.totalBytes);
  reader->moveToThread(thread);
  m_dropThread = thread;
  m_dropReader = reader;
  connect(thread, &QThread::started, reader, &DropReader::run);
  connect(reader, &DropReader::progress, this,
          [this](qint64 read, qint64 total) {
            m_dropProgress->setProgress(read, total);
          });
  connect(reader, &DropReader::finished, this,
          [this, reader, thread]() {
            const QString script = reader->script();
            if (!script.isEmpty())
              page()->runJavaScript(script);
            m_dropReading = false;
            thread->quit();
            // Anything dropped while this was reading goes next, so the bar
            // carries straight on instead of leaving files unattached.
            if (!m_queuedDropPaths.isEmpty()) {
              const QStringList next = m_queuedDropPaths;
              m_queuedDropPaths.clear();
              startDropRead(next, nullptr);
              return;
            }
            m_dropProgress->finish(takeTooLargeMessage());
          });
  connect(thread, &QThread::finished, reader, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
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
