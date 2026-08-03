#include "quickcompose.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

QuickCompose::QuickCompose(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint |
                          Qt::WindowStaysOnTopHint | Qt::Tool) {
  setObjectName(QStringLiteral("quickCompose"));
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_TranslucentBackground, false);
  setFixedWidth(420);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 14);
  root->setSpacing(8);

  auto *title = new QLabel(tr("Quick message"), this);
  QFont f = title->font();
  f.setBold(true);
  title->setFont(f);
  root->addWidget(title);

  m_recipient = new QLineEdit(this);
  m_recipient->setPlaceholderText(tr("Contact name or phone number"));
  m_recipient->setClearButtonEnabled(true);
  root->addWidget(m_recipient);

  m_message = new QLineEdit(this);
  m_message->setPlaceholderText(tr("Message (press Enter to send)"));
  m_message->setClearButtonEnabled(true);
  root->addWidget(m_message);

  auto *send = new QPushButton(tr("Send"), this);
  send->setDefault(true);
  root->addWidget(send);

  connect(send, &QPushButton::clicked, this, &QuickCompose::trySend);
  // Enter in either field advances to send.
  connect(m_recipient, &QLineEdit::returnPressed, this, [this]() {
    if (m_recipient->text().trimmed().isEmpty())
      return;
    m_message->setFocus();
  });
  connect(m_message, &QLineEdit::returnPressed, this, &QuickCompose::trySend);
}

void QuickCompose::popUp() {
  m_recipient->clear();
  m_message->clear();
  adjustSize();
  // Centre on the screen the cursor is on (where the user is looking).
  QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  if (screen) {
    const QRect g = screen->geometry();
    move(g.center().x() - width() / 2, g.center().y() - height() / 2);
  }
  show();
  raise();
  activateWindow();
  m_recipient->setFocus();
}

void QuickCompose::trySend() {
  const QString to = m_recipient->text().trimmed();
  const QString msg = m_message->text().trimmed();
  if (to.isEmpty() || msg.isEmpty())
    return; // nothing to do; leave the box open for the user to complete
  emit submitted(to, msg);
  close();
}

void QuickCompose::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QWidget::keyPressEvent(event);
}

bool QuickCompose::event(QEvent *event) {
  // Dismiss when the overlay loses focus, like a spotlight box.
  if (event->type() == QEvent::WindowDeactivate && isVisible())
    close();
  return QWidget::event(event);
}
