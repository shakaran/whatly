#ifndef PAGEBRIDGE_H
#define PAGEBRIDGE_H

#include <QObject>

// The one object WhatsApp's page can reach, over QWebChannel. Buttons Whatly
// injects into the page (see WebTweaks) call into here; nothing else is
// exposed, and the page cannot reach anything this class does not declare as a
// slot.
class PageBridge : public QObject {
  Q_OBJECT
public:
  explicit PageBridge(QObject *parent = nullptr) : QObject(parent) {}

public slots:
  // Invoked by the buttons injected next to the profile avatar.
  void toggleTheme() { emit themeToggleRequested(); }
  void togglePrivacyBlur() { emit privacyBlurToggleRequested(); }
  void toggleChatListStrip() { emit chatListStripToggleRequested(); }

  // Invoked by the injected zoom buttons: adjust the page zoom live.
  void zoomIn() { emit zoomInRequested(); }
  void zoomOut() { emit zoomOutRequested(); }
  void zoomReset() { emit zoomResetRequested(); }

  // Invoked by the scheduled-message sender once it has clicked Send (or given
  // up), so the schedule can mark the message sent or failed.
  void scheduledMessageResult(const QString &id, bool ok, const QString &error) {
    emit scheduledMessageFinished(id, ok, error);
  }

  // Invoked by the auto-reply observer for each new incoming message in the open
  // conversation, so MainWindow can evaluate the rules and reply.
  void incomingMessage(const QString &text) { emit incomingMessageReceived(text); }

signals:
  void themeToggleRequested();
  void privacyBlurToggleRequested();
  void chatListStripToggleRequested();
  void zoomInRequested();
  void zoomOutRequested();
  void zoomResetRequested();
  void scheduledMessageFinished(const QString &id, bool ok,
                                const QString &error);
  void incomingMessageReceived(const QString &text);
};

#endif // PAGEBRIDGE_H
