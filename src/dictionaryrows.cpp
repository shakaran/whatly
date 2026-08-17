#include "dictionaryrows.h"
#include "dictionaries.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QStyle>

#include <algorithm>

namespace {

// Human size, in the same words the catalogue's issue lists them in ("6.5 MB").
QString humanSize(qint64 bytes) {
  if (bytes <= 0)
    return QString();
  const double mb = double(bytes) / (1024.0 * 1024.0);
  return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

// Room around the button at the right-hand end of a row, and between it and the
// note beside it. Small: this is a drop-down list, not a page of settings.
constexpr int kMargin = 4;
constexpr int kGap = 6;
constexpr int kMaxButton = 20;

// The note a row shows just left of its button: how far a download has got, or
// what the row is when nothing is happening to it.
QString noteText(const QModelIndex &index) {
  const int progress = index.data(DictionaryRows::ProgressRole).toInt();
  if (progress >= 0)
    return QStringLiteral("%1%").arg(progress);
  if (progress == DictionaryRows::Failed)
    return QObject::tr("failed");
  const bool installed = index.data(DictionaryRows::InstalledRole).toBool();
  const auto action = static_cast<DictionaryRows::Action>(
      index.data(DictionaryRows::ActionRole).toInt());
  if (installed)
    // A bundled dictionary is the one installed row with no button, so say why it
    // has none rather than leaving a gap where every other row has a control.
    return action == DictionaryRows::Action::None ? QObject::tr("bundled")
                                                 : QString();
  return humanSize(index.data(DictionaryRows::SizeRole).toLongLong());
}

// The two marks are painted rather than loaded, so they follow the palette. The
// bundled icon set is dark-on-transparent and would vanish on a dark theme, and
// the style's own standard icons are a platform lottery — a down arrow drawn in
// the same pen as the text is the same mark everywhere.
void paintDownloadMark(QPainter *p, const QRect &box, const QColor &colour) {
  const QRectF r(box);
  const qreal cx = r.center().x();
  const qreal top = r.top() + r.height() * 0.18;
  const qreal tip = r.top() + r.height() * 0.62;
  const qreal wing = r.width() * 0.24;
  QPen pen(colour, qMax(1.4, r.height() / 12.0));
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p->setPen(pen);
  p->drawLine(QPointF(cx, top), QPointF(cx, tip));
  QPainterPath head;
  head.moveTo(cx - wing, tip - wing);
  head.lineTo(cx, tip);
  head.lineTo(cx + wing, tip - wing);
  p->drawPath(head);
  // The line it lands on: an arrow alone could mean anything, an arrow onto a
  // surface means "bring it down here".
  p->drawLine(QPointF(r.left() + r.width() * 0.22, r.bottom() - r.height() * 0.2),
              QPointF(r.right() - r.width() * 0.22, r.bottom() - r.height() * 0.2));
}

// The wait, drawn where the tick box will be once the file is here: a three-quarter
// arc turning on the spot, so a row that is fetching something says so even when
// the per-cent beside it is standing still.
void paintSpinner(QPainter *p, const QRect &box, const QColor &colour, int angle) {
  const QRectF r = QRectF(box).adjusted(2, 2, -2, -2);
  QPen pen(colour, qMax(1.5, r.height() / 7.0));
  pen.setCapStyle(Qt::RoundCap);
  p->setPen(pen);
  p->setBrush(Qt::NoBrush);
  p->drawArc(r, -angle * 16, 270 * 16);
}

void paintTrashMark(QPainter *p, const QRect &box, const QColor &colour) {
  const QRectF r(box);
  QPen pen(colour, qMax(1.4, r.height() / 12.0));
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p->setPen(pen);
  const qreal lid = r.top() + r.height() * 0.28;
  const qreal side = r.width() * 0.2;
  // The handle above the lid, the lid across, and a body that tapers slightly.
  p->drawLine(QPointF(r.center().x() - r.width() * 0.12, lid - r.height() * 0.1),
              QPointF(r.center().x() + r.width() * 0.12, lid - r.height() * 0.1));
  p->drawLine(QPointF(r.left() + side * 0.6, lid), QPointF(r.right() - side * 0.6, lid));
  QPainterPath body;
  body.moveTo(r.left() + side, lid + r.height() * 0.06);
  body.lineTo(r.left() + side * 1.5, r.bottom() - r.height() * 0.14);
  body.lineTo(r.right() - side * 1.5, r.bottom() - r.height() * 0.14);
  body.lineTo(r.right() - side, lid + r.height() * 0.06);
  p->drawPath(body);
}

} // namespace

namespace DictionaryRows {

QList<Row> build(const QStringList &installed, const QStringList &removable,
                 const QList<DictionaryEntry> &catalog) {
  QList<Row> rows;
  QSet<QString> seen;

  const auto add = [&](const QString &code, qint64 size, bool downloadable) {
    if (code.isEmpty() || seen.contains(code))
      return;
    seen.insert(code);
    Row row;
    row.code = code;
    row.label = Dictionaries::languageLabel(code);
    row.installed = installed.contains(code);
    row.downloadSize = size;
    if (row.installed)
      row.action = removable.contains(code) ? Action::Delete : Action::None;
    else
      row.action = downloadable ? Action::Download : Action::None;
    rows.append(row);
  };

  for (const DictionaryEntry &entry : catalog)
    // No sha256 means the manifest cannot vouch for the file, and a download that
    // cannot be verified is not offered: Chromium parses the .bdic, so a wrong one
    // must never reach the engine.
    add(entry.code, entry.size, !entry.sha256.isEmpty());
  for (const QString &code : installed)
    add(code, 0, false); // on disk, catalogue or no catalogue

  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
    const int by = a.label.localeAwareCompare(b.label);
    return by != 0 ? by < 0 : a.code < b.code;
  });
  return rows;
}

QString tooltip(const Row &row) {
  QStringList lines;
  const QFileInfo file(QDir(Dictionaries::dictionaryPath())
                           .filePath(row.code + QStringLiteral(".bdic")));
  if (row.installed && file.exists()) {
    lines << QObject::tr("Size: %1").arg(humanSize(file.size()));
    lines << QObject::tr("Installed: %1")
                 .arg(QLocale().toString(file.birthTime().isValid()
                                             ? file.birthTime()
                                             : file.lastModified(),
                                         QLocale::ShortFormat));
    if (Dictionaries::isBundled(row.code))
      lines << QObject::tr("Shipped with Whatly");
  } else if (row.downloadSize > 0) {
    lines << QObject::tr("Download size: %1").arg(humanSize(row.downloadSize));
  }
  // What the button does, since it is a mark rather than a word.
  if (row.action == Action::Download)
    lines << QObject::tr("Click the arrow to download it.");
  else if (row.action == Action::Delete)
    lines << QObject::tr("The bin removes it; it can be downloaded again.");
  lines << row.code; // the .bdic basename, which is what Chromium is given
  return lines.join(QLatin1Char('\n'));
}

} // namespace DictionaryRows

DictionaryRowDelegate::DictionaryRowDelegate(QAbstractItemView *view)
    : QStyledItemDelegate(view), m_view(view) {
  m_clock.start(); // the spinner's angle comes off this, not off a per-row counter
}

QRect DictionaryRowDelegate::actionRect(const QRect &row) {
  const int side = qMin(row.height() - 2 * kMargin + 4, kMaxButton);
  if (side <= 0 || row.width() < side + 2 * kMargin)
    return QRect();
  return QRect(row.right() - kMargin - side, row.top() + (row.height() - side) / 2,
               side, side);
}

QRect DictionaryRowDelegate::noteRect(const QRect &row) {
  const QRect button = actionRect(row);
  if (button.isEmpty())
    return QRect();
  return QRect(row.left(), row.top(), button.left() - kGap - row.left(),
               row.height());
}

void DictionaryRowDelegate::paint(QPainter *painter,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
  const QWidget *widget = option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // The background across the whole row first, so the selection reaches the row's
  // right edge even though the language's name is drawn narrower than the row.
  QStyleOptionViewItem back(option);
  initStyleOption(&back, index);
  back.text.clear();
  back.icon = QIcon();
  back.features &= ~QStyleOptionViewItem::HasCheckIndicator;
  back.features &= ~QStyleOptionViewItem::HasDecoration;
  style->drawControl(QStyle::CE_ItemViewItem, &back, painter, widget);

  const QRect button = actionRect(option.rect);
  const QString note = noteText(index);
  const int noteWidth =
      note.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(note) + kGap;
  const int progress = index.data(DictionaryRows::ProgressRole).toInt();
  const bool waiting = progress >= 0; // a download of this language is in flight

  // The tick box and the language, in the space left over. Narrowing the rect is
  // what makes a long name elide before the note rather than run under it.
  QStyleOptionViewItem body(option);
  if (!button.isEmpty())
    body.rect.setRight(button.left() - kGap - noteWidth);
  if (waiting) {
    // The spinner takes the tick box's place while the file is on its way, and the
    // real tick box comes back — ticked — the moment it lands. An empty greyed box
    // beside a language being fetched said nothing about what was happening.
    QStyleOptionViewItem hide(body);
    initStyleOption(&hide, index);
    hide.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    hide.checkState = Qt::Unchecked;
    style->drawControl(QStyle::CE_ItemViewItem, &hide, painter, widget);
  } else {
    QStyledItemDelegate::paint(painter, body, index);
  }

  const bool selected = option.state & QStyle::State_Selected;
  const QColor ink = selected ? option.palette.highlightedText().color()
                              : option.palette.text().color();

  if (waiting) {
    // Where the style would have drawn the tick box, so the two occupy one slot.
    QStyleOptionViewItem where(option);
    initStyleOption(&where, index);
    const QRect box = style->subElementRect(
        QStyle::SE_ItemViewItemCheckIndicator, &where, widget);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    paintSpinner(painter, box.isEmpty() ? option.rect : box, ink,
                 int(m_clock.elapsed() / 3 % 360));
    painter->restore();
  }

  if (button.isEmpty())
    return;

  if (!note.isEmpty()) {
    painter->save();
    painter->setPen(selected
                        ? ink
                        : option.palette.color(QPalette::Disabled, QPalette::Text));
    painter->drawText(QRect(button.left() - kGap - noteWidth, option.rect.top(),
                            noteWidth - kGap, option.rect.height()),
                      Qt::AlignRight | Qt::AlignVCenter, note);
    painter->restore();
  }

  const auto action =
      static_cast<DictionaryRows::Action>(index.data(DictionaryRows::ActionRole).toInt());
  if (action == DictionaryRows::Action::None)
    return;

  // Lit when the pointer is on it, so an icon in a list still reads as a button.
  const bool hot =
      m_view && m_view->viewport()->rect().contains(
                    m_view->viewport()->mapFromGlobal(QCursor::pos())) &&
      button.contains(m_view->viewport()->mapFromGlobal(QCursor::pos()));

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  if (hot) {
    QColor glow = option.palette.highlight().color();
    glow.setAlpha(selected ? 90 : 60);
    painter->setPen(Qt::NoPen);
    painter->setBrush(glow);
    painter->drawRoundedRect(button, 4, 4);
  }
  painter->setBrush(Qt::NoBrush);
  const QRect mark = button.adjusted(3, 3, -3, -3);
  if (action == DictionaryRows::Action::Download)
    paintDownloadMark(painter, mark, ink);
  else
    paintTrashMark(painter, mark, ink);
  painter->restore();
}

QSize DictionaryRowDelegate::sizeHint(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const {
  QSize size = QStyledItemDelegate::sizeHint(option, index);
  // Room for the widest note the list can show plus the button, so a name is never
  // squeezed by them; and enough height for the button to be worth pressing.
  const int note = option.fontMetrics.horizontalAdvance(QObject::tr("bundled"));
  size.setWidth(size.width() + note + kMaxButton + 2 * kGap + 2 * kMargin);
  size.setHeight(qMax(size.height(), kMaxButton + 2));
  return size;
}
