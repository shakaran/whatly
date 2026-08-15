#ifndef SETTINGSSEARCH_H
#define SETTINGSSEARCH_H

#include <QList>
#include <QString>

class QGroupBox;
class QLayout;
class QLayoutItem;
class QWidget;

// Searching the Settings page (issue #39). The controls found are the real ones,
// still connected and still working where they stand — the page is filtered, not
// replaced by a list of results to click through, which is what VLC's preferences
// search does and what was asked for.
//
// Everything here is about deciding WHAT matches and WHICH widgets go with it.
// Showing and hiding is SettingsWidget's job, since the accordion state it has to
// put back afterwards is its own.
namespace SettingsSearch {

// One filterable line of a section: the widgets that stand or fall together, and
// the words they can be found by.
//
// A row is a horizontal band of the page, which is not the same as one item of
// the section's column. Dogfood round 28: searching "spelling" showed all forty
// keyboard shortcuts, because the shortcuts are a form inside a single host
// widget — one item of the column, and so one row. So the walk goes down through
// anything that stacks (a column, a grid, a form, a group of its own) and stops
// at anything that sits side by side (a label with its control), which is the
// band a reader sees as one line.
struct Row {
  QList<QWidget *> widgets;
  QString haystack;
  // The group this row lives inside, if any: a form host, or a nested group box.
  // It is hidden when none of its rows survives the filter, so no empty frame is
  // left standing where a group used to be.
  QWidget *container = nullptr;
};

// Case- and accent-insensitive, so "basico" finds "Básico" and "notificacion"
// finds "notificación". Accents are stripped rather than mapped: the point is to
// let someone type what is on their keyboard.
QString normalise(const QString &text);

// Every word of the query has to appear somewhere in the haystack, in any order —
// "unload window" finds "unload also minimised and hidden accounts" through its
// tooltip. Substrings count, so a half-typed word matches while it is being
// typed. An empty query matches everything, which is how a cleared box restores
// the page.
bool matches(const QString &haystack, const QString &query);

// What a single widget can be found by: its label, its title, its tooltip, its
// placeholder, and every entry of a combo box. Tooltips are included on purpose —
// they carry the vocabulary the labels have no room for ("memory", "renderer",
// "delta"), which is exactly what someone searches with.
QString textOf(const QWidget *widget);

// The same for a whole layout item, widgets nested inside it included.
QString textOfItem(QLayoutItem *item);

// Every widget in a layout item, in order, nesting included.
QList<QWidget *> widgetsOfItem(QLayoutItem *item);

// The rows of one section's group box, in the order they appear, nesting walked
// through as described above. A group box with no layout of its own has none,
// which is not an error — a section built some other way is simply left alone by
// the search.
QList<Row> rowsOf(const QGroupBox *body);

} // namespace SettingsSearch

#endif // SETTINGSSEARCH_H
