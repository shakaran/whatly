#include "customtitlebar.h"
#include "settingsmanager.h"
#include "utils.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
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

    m_title = new QLabel(window->windowTitle(), this);
    layout->addWidget(m_title, 1);

    // The version goes at the far end of the row, so the title keeps the whole
    // left-hand side it has always had and the two never fight for the middle.
    // Same watermark treatment as the merged strip, so the answer to "which
    // build is this?" looks the same wherever it is read.
    m_version = new QLabel(Utils::versionLabel(), this);
    m_version->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_version);
  } else {
    // No fixed height and no background of its own: the tab strip beside it
    // sets the row's height and draws the row's background, and anything else
    // here would either clip the tabs or paint a band that does not match them.
    // The empty space left of the buttons is what the window is dragged by.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    // With the title bar gone there is nowhere else the version could be shown,
    // so it goes in the space the tabs do not use — the same run of empty strip
    // that the window is dragged by. Ignored horizontally with no minimum, so
    // the layout may shrink it to nothing: the text is then simply cut off,
    // which is the right answer here. The tabs are what the row is for, and this
    // must never push them.
    m_version = new QLabel(Utils::versionLabel(), this);
    m_version->setMinimumWidth(0);
    m_version->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_version->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_version, 1);
  }

  if (m_version) {
    // Transparent to mouse events, or it would swallow the presses that drag the
    // window and the strip it sits in would stop working. That costs it its own
    // tooltip, so the tooltip goes on the bar instead — see below, where it also
    // answers a hover anywhere in the empty run, which is where a hand goes
    // looking.
    m_version->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Small and faint on purpose: it shares a row with the tabs or the title, so
    // it has to read as a watermark rather than as another label competing with
    // them. Being small is also what lets a long build label fit before the
    // space runs out. Point and pixel sizes both handled — a font set in pixels
    // reports -1 for its point size, and scaling that would make the text
    // vanish.
    QFont vf = m_version->font();
    if (vf.pointSizeF() > 0)
      vf.setPointSizeF(qMax(6.0, vf.pointSizeF() * 0.78));
    else if (vf.pixelSize() > 0)
      vf.setPixelSize(qMax(8, int(vf.pixelSize() * 0.78)));
    m_version->setFont(vf);
    // The colour is the background nudged a fifth of the way towards the text
    // colour. Dimming the text colour instead was still too loud, and it also
    // needs no light/dark special case: whichever way round the theme is, this
    // lands just off the background.
    QPalette vp = m_version->palette();
    const QColor bg = vp.color(QPalette::Window);
    const QColor fg = vp.color(QPalette::WindowText);
    const auto toward = [](int from, int to) {
      return from + int(0.2 * (to - from));
    };
    vp.setColor(QPalette::WindowText,
                QColor(toward(bg.red(), fg.red()), toward(bg.green(), fg.green()),
                       toward(bg.blue(), fg.blue())));
    m_version->setPalette(vp);
    // The whole bar answers the hover: the label is transparent to the mouse,
    // and the version is the only thing in this row anyone would hover for.
    setToolTip(Utils::appNameWithVersion());
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
    // Merged mode has neither label — the tabs say which window this is.
    if (event->type() == QEvent::WindowTitleChange && m_title)
      m_title->setText(m_window->windowTitle());
    else if (event->type() == QEvent::WindowIconChange && m_icon)
      m_icon->setPixmap(m_window->windowIcon().pixmap(18, 18));
    else if (event->type() == QEvent::WindowStateChange)
      refreshMaximizeIcon();
  }
  return QWidget::eventFilter(watched, event);
}
