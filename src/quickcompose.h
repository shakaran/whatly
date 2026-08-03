#ifndef QUICKCOMPOSE_H
#define QUICKCOMPOSE_H

#include <QWidget>

class QLineEdit;

// Quick-compose overlay (idea #4): a small, frameless, always-on-top box summoned
// by a global hotkey (Ctrl+Alt+N) to send a message without opening the full app.
// Type a contact name or phone number, type the message, press Enter. It closes
// itself on send, on Escape, or when it loses focus. It does no sending itself —
// it emits submitted() and MainWindow routes it through the existing web-send
// path (which works even while the main window is hidden).
class QuickCompose : public QWidget {
  Q_OBJECT
public:
  explicit QuickCompose(QWidget *parent = nullptr);

  // Show centred on the screen under the cursor, cleared and focused.
  void popUp();

signals:
  // recipient: a contact/group name or a phone number; message: the text.
  void submitted(const QString &recipient, const QString &message);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  bool event(QEvent *event) override; // close on deactivation

private slots:
  void trySend();

private:
  QLineEdit *m_recipient = nullptr;
  QLineEdit *m_message = nullptr;
};

#endif // QUICKCOMPOSE_H
