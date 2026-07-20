#include "customtitlebar.h"
#include "settingsmanager.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>
#include <QWindow>

namespace {
const char kSettingsKey[] = "customWindowFrame";
} // namespace

CustomTitleBar::CustomTitleBar(QWidget *window, QWidget *parent)
    : QWidget(parent), m_window(window) {
  setObjectName(QStringLiteral("customTitleBar"));
  setAutoFillBackground(true);
  setFixedHeight(34);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 0, 4, 0);
  layout->setSpacing(6);

  m_icon = new QLabel(this);
  m_icon->setPixmap(window->windowIcon().pixmap(18, 18));
  layout->addWidget(m_icon);

  m_title = new QLabel(window->windowTitle(), this);
  layout->addWidget(m_title, 1);

  const QStyle *st = style();
  auto makeButton = [&](QStyle::StandardPixmap pm, const QString &tip) {
    auto *b = new QToolButton(this);
    b->setAutoRaise(true);
    b->setIcon(st->standardIcon(pm));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(b);
    return b;
  };

  auto *minButton = makeButton(QStyle::SP_TitleBarMinButton, tr("Minimise"));
  connect(minButton, &QToolButton::clicked, this,
          [this]() { m_window->showMinimized(); });

  m_maxButton = makeButton(QStyle::SP_TitleBarMaxButton, tr("Maximise"));
  connect(m_maxButton, &QToolButton::clicked, this,
          [this]() { toggleMaximized(); });

  auto *closeButton = makeButton(QStyle::SP_TitleBarCloseButton, tr("Close"));
  connect(closeButton, &QToolButton::clicked, this,
          [this]() { m_window->close(); });

  // Keep the icon/title/maximise glyph in sync with the window.
  m_window->installEventFilter(this);
  refreshMaximizeIcon();
}

bool CustomTitleBar::isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kSettingsKey), false)
      .toBool();
}

void CustomTitleBar::setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kSettingsKey),
                                                  enabled);
}

void CustomTitleBar::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_window->windowHandle()) {
    // Hand the drag to the compositor; the only portable way on Wayland.
    m_window->windowHandle()->startSystemMove();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    toggleMaximized();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CustomTitleBar::toggleMaximized() {
  if (m_window->isMaximized())
    m_window->showNormal();
  else
    m_window->showMaximized();
  refreshMaximizeIcon();
}

void CustomTitleBar::refreshMaximizeIcon() {
  if (!m_maxButton)
    return;
  m_maxButton->setIcon(style()->standardIcon(
      m_window->isMaximized() ? QStyle::SP_TitleBarNormalButton
                              : QStyle::SP_TitleBarMaxButton));
}

bool CustomTitleBar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_window) {
    if (event->type() == QEvent::WindowTitleChange)
      m_title->setText(m_window->windowTitle());
    else if (event->type() == QEvent::WindowIconChange)
      m_icon->setPixmap(m_window->windowIcon().pixmap(18, 18));
    else if (event->type() == QEvent::WindowStateChange)
      refreshMaximizeIcon();
  }
  return QWidget::eventFilter(watched, event);
}
