#include "dropprogress.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Wide enough for a few long file names; it grows taller for more than that.
constexpr int kMaxWidth = 520;
constexpr int kMargin = 18;
// How long a closing message stays up before the bar disappears.
constexpr int kMessageMs = 6000;
// Grace period before the bar appears, so a quick drop never flashes one up.
constexpr int kShowDelayMs = 400;
} // namespace

DropProgress::DropProgress(QWidget *host)
    : QWidget(host ? host->window() : nullptr), m_host(host) {
  // Purely informational, so it must never swallow a click meant for the chat.
  setAttribute(Qt::WA_TransparentForMouseEvents);

  m_label = new QLabel(this);
  m_label->setWordWrap(true);
  // Weighted like the page's own notices (the over-16MB and unsupported-video
  // ones), which are bolder than a default label and were what this sat next to.
  QFont labelFont = m_label->font();
  labelFont.setBold(true);
  labelFont.setPointSizeF(labelFont.pointSizeF() * 1.1);
  m_label->setFont(labelFont);
  m_bar = new QProgressBar(this);
  m_bar->setTextVisible(false);
  m_bar->setFixedHeight(6);

  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(12, 10, 12, 10);
  v->setSpacing(8);
  v->addWidget(m_label);
  v->addWidget(m_bar);

  // Follows the palette so it reads on both the light and the dark theme.
  setStyleSheet(QStringLiteral(
      "DropProgress{background:palette(window);border:1px solid palette(mid);"
      "border-radius:8px}"));

  m_showTimer = new QTimer(this);
  m_showTimer->setSingleShot(true);
  connect(m_showTimer, &QTimer::timeout, this, [this]() {
    reposition();
    show();
    raise();
  });

  // Takes a closing message down after a while. Owned (not an anonymous
  // singleShot) so a new drop can cancel a pending hide from the previous one.
  m_hideTimer = new QTimer(this);
  m_hideTimer->setSingleShot(true);
  connect(m_hideTimer, &QTimer::timeout, this, [this]() { hide(); });

  if (m_host) {
    m_host->installEventFilter(this);
    if (m_host->window() != m_host)
      m_host->window()->installEventFilter(this);
  }
  hide();
}

void DropProgress::begin() {
  // Cancel a pending hide from a previous closing message: without this, that
  // stale timer would take this new read's bar down mid-way (issue raised in
  // review), leaving the user with no feedback.
  m_hideTimer->stop();
  m_label->setText(tr("Attaching…"));
  m_bar->setRange(0, 100);
  m_bar->setValue(0);
  m_bar->show();
  reposition();
  // Most drops finish in well under a second, and a panel that appears only to
  // vanish again reads as a glitch. Wait a moment: if the read is already done
  // by then, nothing is ever shown.
  m_showTimer->start(kShowDelayMs);
}

void DropProgress::setProgress(qint64 bytesRead, qint64 bytesTotal) {
  if (bytesTotal <= 0)
    return;
  m_bar->setValue(static_cast<int>((bytesRead * 100) / bytesTotal));
}

void DropProgress::finish(const QString &message) {
  m_showTimer->stop(); // the read is over; no point appearing now
  if (message.isEmpty()) {
    hide();
    return;
  }
  // A file left out of the drop must not go unnoticed the way a terminal-only
  // warning did, so this shows even when nothing was read at all and the bar
  // itself was therefore never on screen.
  m_label->setText(message);
  m_bar->hide();
  reposition();
  show();
  raise();
  m_hideTimer->start(kMessageMs);
}

bool DropProgress::eventFilter(QObject *watched, QEvent *event) {
  if (isVisible() && (event->type() == QEvent::Resize ||
                      event->type() == QEvent::Move))
    reposition();
  return QWidget::eventFilter(watched, event);
}

void DropProgress::reposition() {
  if (!m_host || !parentWidget())
    return;
  const int w = qMin(kMaxWidth, qMax(160, m_host->width() - 2 * kMargin));
  // Width first, then measure: the label wraps, so its height only means
  // anything once it knows how wide it will be. Ask the layout for the height
  // that width needs, so several long file names make the panel taller instead
  // of being clipped by it.
  resize(w, height());
  int h = sizeHint().height();
  if (QLayout *l = layout()) {
    l->activate();
    if (l->hasHeightForWidth())
      h = l->heightForWidth(w);
  }
  resize(w, h);
  // Bottom-centre of the view, expressed in the window's coordinates.
  const QPoint hostTopLeft = m_host->mapTo(parentWidget(), QPoint(0, 0));
  const int x = hostTopLeft.x() + (m_host->width() - width()) / 2;
  const int y = hostTopLeft.y() + m_host->height() - height() - kMargin;
  move(x, y);
}
