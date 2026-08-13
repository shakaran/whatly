#include "settingssearch.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLayout>
#include <QLayoutItem>
#include <QMap>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace SettingsSearch {

QString normalise(const QString &text) {
  // Decompose first, then drop every combining mark: that turns "á" into "a"
  // without a table of language-specific rules, and leaves scripts that do not
  // decompose (Cyrillic, Arabic, the CJK sets) untouched rather than mangled.
  const QString decomposed = text.normalized(QString::NormalizationForm_KD);
  QString out;
  out.reserve(decomposed.size());
  for (const QChar c : decomposed) {
    if (c.category() == QChar::Mark_NonSpacing ||
        c.category() == QChar::Mark_SpacingCombining)
      continue;
    // Hyphens and the rest of the punctuation go as well, so "spellcheck" finds
    // "spell-check" and "wifi" would find "Wi-Fi". Whether a compound is written
    // with a hyphen is not something anyone should have to guess at while typing.
    if (c.isPunct())
      continue;
    out.append(c.toCaseFolded());
  }
  return out;
}

bool matches(const QString &haystack, const QString &query) {
  const QStringList words =
      normalise(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (words.isEmpty())
    return true; // nothing typed: everything is a match, so the page comes back
  const QString hay = normalise(haystack);
  for (const QString &word : words)
    if (!hay.contains(word))
      return false;
  return true;
}

QString textOf(const QWidget *widget) {
  if (!widget)
    return QString();
  QStringList parts;
  // By property rather than by class, so a control this code has never heard of
  // is still searchable by whatever it says on it.
  for (const char *name : {"text", "title", "placeholderText"}) {
    const QVariant v = widget->property(name);
    if (v.isValid() && !v.toString().isEmpty())
      parts << v.toString();
  }
  if (!widget->toolTip().isEmpty())
    parts << widget->toolTip();
  if (const auto *combo = qobject_cast<const QComboBox *>(widget))
    for (int i = 0; i < combo->count(); ++i)
      parts << combo->itemText(i);
  return parts.join(QLatin1Char(' '));
}

QList<QWidget *> widgetsOfItem(QLayoutItem *item) {
  QList<QWidget *> out;
  if (!item)
    return out;
  if (QWidget *w = item->widget()) {
    out << w;
    return out; // its children go with it; they are shown and hidden as one
  }
  if (QLayout *l = item->layout())
    for (int i = 0; i < l->count(); ++i)
      out << widgetsOfItem(l->itemAt(i));
  return out;
}

QString textOfItem(QLayoutItem *item) {
  QStringList parts;
  if (!item)
    return QString();
  if (const QWidget *w = item->widget()) {
    parts << textOf(w);
    // A nested group, or a row built as a widget of its own: its contents are
    // what it is found by, and they are not reachable through the layout above.
    for (const QWidget *child : w->findChildren<QWidget *>())
      parts << textOf(child);
  } else if (QLayout *l = item->layout()) {
    for (int i = 0; i < l->count(); ++i)
      parts << textOfItem(l->itemAt(i));
  }
  parts.removeAll(QString());
  return parts.join(QLatin1Char(' '));
}

namespace {

// A group of controls rather than one control. Recursing into these is what keeps
// a row down to one line: a form host, or a group box nested in a section. A spin
// box and an editable combo have layouts of their own too, and those must stay
// whole, hence the narrow test — a bare QWidget used as a host, or a group box.
bool isGroupOfControls(const QWidget *w) {
  if (!w || !w->layout())
    return false;
  return qobject_cast<const QGroupBox *>(w) != nullptr ||
         QLatin1String(w->metaObject()->className()) == QLatin1String("QWidget");
}

void collectRows(QLayout *layout, QWidget *container, QList<Row> *out);

// One band, from the items that share it. A band holding nothing but a group of
// controls is not a row at all: the search goes inside it instead.
void addBand(const QList<QLayoutItem *> &items, QWidget *container,
             QList<Row> *out) {
  if (items.size() == 1) {
    if (QWidget *w = items.first()->widget()) {
      if (isGroupOfControls(w)) {
        collectRows(w->layout(), w, out);
        return;
      }
    } else if (QLayout *inner = items.first()->layout()) {
      collectRows(inner, container, out); // a nested layout stacks or does not
      return;
    }
  }
  Row row;
  row.container = container;
  QStringList text;
  for (QLayoutItem *item : items) {
    row.widgets << widgetsOfItem(item);
    text << textOfItem(item);
  }
  if (row.widgets.isEmpty())
    return; // a spacer or a stretch: nothing to hide, nothing to find
  text.removeAll(QString());
  row.haystack = text.join(QLatin1Char(' '));
  *out << row;
}

void collectRows(QLayout *layout, QWidget *container, QList<Row> *out) {
  if (!layout)
    return;
  // A grid: one band per grid row, so a table of settings filters row by row.
  if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
    QMap<int, QList<QLayoutItem *>> byRow;
    for (int i = 0; i < grid->count(); ++i) {
      int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
      grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
      byRow[row] << grid->itemAt(i);
    }
    for (auto it = byRow.constBegin(); it != byRow.constEnd(); ++it)
      addBand(it.value(), container, out);
    return;
  }
  // A form: label and field are one band, which is exactly what a form is.
  if (auto *form = qobject_cast<QFormLayout *>(layout)) {
    for (int row = 0; row < form->rowCount(); ++row) {
      QList<QLayoutItem *> items;
      for (const QFormLayout::ItemRole role :
           {QFormLayout::LabelRole, QFormLayout::FieldRole,
            QFormLayout::SpanningRole})
        if (QLayoutItem *item = form->itemAt(row, role))
          items << item;
      addBand(items, container, out);
    }
    return;
  }
  // Side by side: one band. Anything else stacks, so each item is its own.
  if (qobject_cast<QHBoxLayout *>(layout)) {
    QList<QLayoutItem *> items;
    for (int i = 0; i < layout->count(); ++i)
      items << layout->itemAt(i);
    addBand(items, container, out);
    return;
  }
  for (int i = 0; i < layout->count(); ++i)
    addBand({layout->itemAt(i)}, container, out);
}

} // namespace

QList<Row> rowsOf(const QGroupBox *body) {
  QList<Row> rows;
  if (!body)
    return rows;
  collectRows(body->layout(), nullptr, &rows);
  return rows;
}

} // namespace SettingsSearch
