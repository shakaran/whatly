#ifndef NOTIFICATIONREPLY_H
#define NOTIFICATIONREPLY_H

#include <QObject>
#include <QString>
#include <QStringList>

// Inline-reply notifications (idea #2). Talks to org.freedesktop.Notifications
// directly (libnotify does not surface the reply text), so a message can be
// answered from the notification's own text field. This is a KDE/GNOME
// extension advertised via the "inline-reply" server capability; where it is
// absent, isAvailable() is false and callers fall back to a plain notification.
//
// hasInlineReply() is a pure helper over a capability list and is unit-tested;
// the rest is D-Bus I/O and is only compiled on Linux.

namespace NotificationReplyUtil {
// True when the server capability list advertises inline reply.
bool hasInlineReply(const QStringList &capabilities);
} // namespace NotificationReplyUtil

class QImage;

class NotificationReply : public QObject {
  Q_OBJECT
public:
  explicit NotificationReply(QObject *parent = nullptr);

  // Service present and advertising the inline-reply capability.
  bool isAvailable() const { return m_available; }

  // Show a notification carrying an inline reply field and an "open" action.
  // Returns the server-assigned id, or 0 on failure (caller should fall back).
  quint32 notify(const QString &appName, const QString &title,
                 const QString &body, const QImage &icon, int timeoutMs,
                 const QString &replyLabel, const QString &replyPlaceholder);

  // Ask the server to withdraw a notification we posted.
  void close(quint32 id);

signals:
  void replied(quint32 id, const QString &text);
  void actionInvoked(quint32 id, const QString &actionKey);
  void closed(quint32 id, quint32 reason);

private:
  bool m_available = false;
};

#endif // NOTIFICATIONREPLY_H
