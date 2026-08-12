#ifndef DICTIONARYROWS_H
#define DICTIONARYROWS_H

#include <QList>
#include <QString>
#include <QStyledItemDelegate>

#include "dictionarymanager.h"

// One line of the spell-check language list (issue #46). The list is the picker
// the user already opens to choose languages, so choosing a language and getting
// hold of it are the same list rather than two: every language known gets a row,
// a row you have is a tick box with a Delete button, and a row you do not have is
// greyed with a Download button. That is the whole state machine, and the reason
// the ~43 MB of dictionaries need not all ship in every install.
namespace DictionaryRows {

// What the button at the right-hand end of a row does. A bundled dictionary has
// no button: it is a symlink into the read-only bundle, so deleting it would only
// relink on the next launch.
enum class Action {
  None,
  Download,
  Delete,
};

// The item roles the delegate paints from and the click handler reads. The name
// and the tick live in Qt::DisplayRole and Qt::CheckStateRole as in any list, so
// the ordinary parts of the row need no special-casing anywhere.
enum Role {
  CodeRole = Qt::UserRole, // the .bdic basename Chromium is given, e.g. "pt_PT"
  InstalledRole,           // bool: on disk, so it can be ticked
  ActionRole,              // Action
  ProgressRole,            // per-cent while downloading; Idle or Failed otherwise
  SizeRole,                // download size from the manifest, for a row not held
};

// ProgressRole's two non-per-cent values, so a row can say what happened to a
// download without a role of its own for it.
enum Progress {
  Idle = -1,
  Failed = -2,
};

struct Row {
  QString code;
  QString label; // Dictionaries::languageLabel(code)
  bool installed = false;
  Action action = Action::None;
  qint64 downloadSize = 0; // from the manifest, for a row not yet on disk
};

// Every language worth a row, in the order shown: what is on disk, plus what the
// catalogue offers, named and sorted by the name the row displays. Anything
// installed but not catalogued (a bundled extra, or a .bdic the user dropped in)
// still gets a row, so the list always accounts for what is on disk.
//
// Pure: everything it needs is an argument, which is what makes the three states
// testable without a network or a dictionary directory. `removable` is the subset
// of `installed` that is a real file in the user's own directory.
QList<Row> build(const QStringList &installed, const QStringList &removable,
                 const QList<DictionaryEntry> &catalog);

// What hovering a row says: its size, when it arrived and the code Chromium is
// given, read off the file itself. No version and no upstream date, because the
// manifest carries code, size and sha256 only and a version would be invented.
QString tooltip(const Row &row);

} // namespace DictionaryRows

// Draws a row: the tick box and the language on the left, and at the right-hand
// end a note (the download size, "bundled", or how far a download has got) and the
// one button that row offers.
//
// A delegate rather than a widget per row because this list is a combo box's
// popup: it has a model, not a layout. The button's rectangle is a static
// function so the click handler in SettingsWidget::eventFilter hit-tests exactly
// what was painted — the button cannot end up somewhere other than where it is
// pressed.
class DictionaryRowDelegate : public QStyledItemDelegate {
public:
  explicit DictionaryRowDelegate(QAbstractItemView *view);

  // The button's square at the right-hand end of `row`, and the space left of it
  // for the note. Both empty when the row is too narrow to hold them.
  static QRect actionRect(const QRect &row);
  static QRect noteRect(const QRect &row);

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;

private:
  QAbstractItemView *m_view; // for the pointer's position, to light the button
};

#endif // DICTIONARYROWS_H
