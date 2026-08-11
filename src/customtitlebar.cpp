#include "customtitlebar.h"
#include "lingertip.h"
#include "settingsmanager.h"
#include "utils.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>
#include <QWindow>

namespace {
const char kSettingsKey[] = "customWindowFrame";
const char kTabsKey[] = "tabsInTitleBar";
} // namespace

CustomTitleBar::CustomTitleBar(QWidget *window, QWidget *parent, Mode mode)
    : QWidget(parent), m_window(window) {
  setObjectName(QStringLiteral("customTitleBar"));

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 0, 4, 0);
  layout->setSpacing(6);

  if (mode == Mode::Standalone) {
    setAutoFillBackground(true);
    setFixedHeight(34);

    m_icon = new QLabel(this);
    m_icon->setPixmap(window->windowIcon().pixmap(18, 18));
    layout->addWidget(m_icon);

    m_title = new QLabel(barTitle(), this);
    layout->addWidget(m_title, 1);

    // Neither of them takes the mouse. The whole row left of the buttons is what
    // the window is dragged by, and a label that answered a press would put a
    // dead patch in the middle of it — and swallow the hover the bar answers.
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  } else {
    // No fixed height and no background of its own: the tab strip beside it
    // sets the row's height and draws the row's background, and anything else
    // here would either clip the tabs or paint a band that does not match them.
    // The empty space left of the buttons is what the window is dragged by.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    // The same title a standalone bar shows, in the space the tabs do not use —
    // this row is the title bar, so it should say what a title bar says. Small
    // and faint: the tabs are what the row is for, and this must not read as
    // another label competing with them. Ignored horizontally with no minimum,
    // so the layout may shrink it to nothing and the text is simply cut off,
    // which is the right answer here — it must never push the tabs.
    m_title = new QLabel(barTitle(), this);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    Utils::makeWatermark(m_title);
    layout->addWidget(m_title, 1);

    // Transparent to mouse events, or it would swallow the presses that drag the
    // window and the run of strip it sits in would stop working. That costs it
    // its own tooltip, which is no loss: the one worth having here belongs to
    // the whole run, not to wherever the text happens to reach.
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Which build this is, for the one person in a hundred who wants to know.
    // Only in this mode: a bar the system draws has no such run and no tooltip
    // either, so putting it on a bar of ours as well would make it an answer
    // that appears and disappears with a setting.
    LingerTip::install(this, Utils::appNameWithVersion(),
                       // Not over the window buttons; they have their own tips.
                       [this](const QPoint &p) { return childAt(p) == nullptr; });
  }

  const QStyle *st = style();
  auto makeButton = [&](QStyle::StandardPixmap pm, const QString &tip) {
    auto *b = new QToolButton(this);
    b->setAutoRaise(true);
    b->setIcon(st->standardIcon(pm));
    b->setToolTip(tip);
    // Icon-only buttons need an accessible name; without one a screen reader
    // announces nothing at all.
    b->setAccessibleName(tip);
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

QString CustomTitleBar::barTitle() const {
  // What the platform writes on a frame it draws, written here because on this
  // one nothing else will: the application's name and the window's own title,
  // once each. Qt appends the name to every window title at the platform layer,
  // so the title itself must not carry one — that is what made the system's own
  // title bar read "Whatly: WhatsApp — Whatly", and a detached window's read the
  // name twice with a dash on either side of it.
  const QString title = m_window->windowTitle();
  const QString name = QApplication::applicationDisplayName();
  return title.isEmpty() ? name : name + QStringLiteral(": ") + title;
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

bool CustomTitleBar::tabsInTitleBar() {
  // There is no native frame to merge into, so this only means anything once
  // the custom frame is on. Guarding here rather than at the two call sites
  // keeps a stale stored value from producing a window with no way to close it.
  return isEnabled() && SettingsManager::instance()
                            .settings()
                            .value(QLatin1String(kTabsKey), false)
                            .toBool();
}

void CustomTitleBar::setTabsInTitleBar(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kTabsKey),
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
    // Both modes have a title; only a standalone bar has the icon beside it.
    if (event->type() == QEvent::WindowTitleChange && m_title)
      m_title->setText(barTitle());
    else if (event->type() == QEvent::WindowIconChange && m_icon)
      m_icon->setPixmap(m_window->windowIcon().pixmap(18, 18));
    else if (event->type() == QEvent::WindowStateChange)
      refreshMaximizeIcon();
  }
  return QWidget::eventFilter(watched, event);
}
