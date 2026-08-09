#include "lingertip.h"

#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

namespace {
// Long enough that nobody arrives at it on the way somewhere else.
const int kDelayMs = 5000;
} // namespace

void LingerTip::install(QWidget *widget, QString text,
                        std::function<bool(const QPoint &)> eligible) {
  if (widget)
    new LingerTip(widget, std::move(text), std::move(eligible));
}

LingerTip::LingerTip(QWidget *widget, QString text,
                     std::function<bool(const QPoint &)> eligible)
    : QObject(widget), m_widget(widget), m_text(std::move(text)),
      m_eligible(std::move(eligible)) {
  // Without this a move only arrives while a button is held, and this is about a
  // hand resting on something rather than pressing it.
  m_widget->setMouseTracking(true);
  m_widget->installEventFilter(this);

  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  m_timer->setInterval(kDelayMs);
  connect(m_timer, &QTimer::timeout, this, [this]() {
    const QPoint pos = m_widget->mapFromGlobal(QCursor::pos());
    if (m_widget->isVisible() && m_widget->rect().contains(pos) &&
        eligibleAt(pos))
      QToolTip::showText(QCursor::pos(), m_text, m_widget);
  });
}

bool LingerTip::eligibleAt(const QPoint &pos) const {
  return !m_eligible || m_eligible(pos);
}

bool LingerTip::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_widget) {
    switch (event->type()) {
    case QEvent::Enter:
      if (eligibleAt(static_cast<QEnterEvent *>(event)->position().toPoint()))
        m_timer->start();
      break;
    case QEvent::MouseMove: {
      // Only ever take back this tip. Hiding whatever happens to be on screen
      // would put out the tooltip of the tab the pointer has just moved onto.
      if (QToolTip::isVisible() && QToolTip::text() == m_text)
        QToolTip::hideText();
      if (eligibleAt(static_cast<QMouseEvent *>(event)->position().toPoint()))
        m_timer->start();
      else
        m_timer->stop();
      break;
    }
    case QEvent::Leave:
      if (QToolTip::isVisible() && QToolTip::text() == m_text)
        QToolTip::hideText();
      m_timer->stop();
      break;
    default:
      break;
    }
  }
  return QObject::eventFilter(watched, event);
}
