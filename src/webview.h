#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <QKeyEvent>
#include <QWebEngineView>

#include "settingsmanager.h"

class WebView : public QWebEngineView {
  Q_OBJECT

public:
  WebView(QWidget *parent = nullptr);

  // Which account this view belongs to ("" for the default account). Lets the
  // one title/load handler in MainWindow tell the accounts apart.
  QString accountId;

protected:
  void contextMenuEvent(QContextMenuEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // Hands a clipboard image to the page when Qt WebEngine would otherwise drop
  // it. Returns true when it handled the paste, so the native one is skipped.
  bool pasteClipboardImage();

  // Reads the local files from a drop and hands them to WhatsApp Web as an
  // attachment (issue #285). Returns true when at least one file was sent.
  bool dropFiles(const class QMimeData *mime);

  // Reads one batch of dropped files, shows the progress bar, and hands the
  // result to the page. mime is only there for the "nothing could be read"
  // diagnosis and is null for a batch that was queued behind another.
  bool startDropRead(const QStringList &paths, const class QMimeData *mime);

  // The names left out by the size cap, worded for the user and then forgotten.
  QString takeTooLargeMessage();

  // The progress bar shown while a drop is being read, created on the first
  // drop; whether a read is in flight; and files dropped during that read,
  // which are attached after it rather than refused.
  class DropProgress *m_dropProgress = nullptr;
  bool m_dropReading = false;
  QStringList m_queuedDropPaths;
  QStringList m_dropTooLarge;

  // Break a runaway render-process crash loop (issue #28): when the renderer
  // keeps terminating (e.g. repeatedly SIGTERM'd in a Flatpak), stop reloading
  // and re-prompting so the user is not trapped in an endless dialog. Tracks how
  // many terminations happened within a short window.
  qint64 m_lastRenderCrashMs = 0;
  int m_renderCrashCount = 0;
  bool m_renderCrashDialogUp = false;
};

#endif // WEBVIEW_H
