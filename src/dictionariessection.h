#ifndef DICTIONARIESSECTION_H
#define DICTIONARIESSECTION_H

#include <QHash>
#include <QList>
#include <QWidget>

#include "dictionarymanager.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

// The "Spell-check dictionaries" settings section (issue #46): one row per
// language, each carrying its own Download or Delete button and its status, so
// the 45 MB of dictionaries no longer all ship in every install. Fetches the
// catalogue from the published manifest and merges it with what is already
// installed (bundled minimum, or previously downloaded).
class DictionariesSection : public QWidget {
  Q_OBJECT
public:
  // `manager` is shared with the settings' language picker (one catalogue fetch,
  // one set of download signals) and is not owned by this widget. The caller
  // triggers the catalogue fetch after wiring, so both sides receive it.
  explicit DictionariesSection(DictionaryManager *manager,
                               QWidget *parent = nullptr);

signals:
  // The installed set changed (a download finished, or a delete): whoever owns
  // the spell-check language list should re-read it and re-apply.
  void installedChanged();

private:
  void rebuild();                       // repaint the whole list from current state
  void setStatus(const QString &text);  // the single-line message before the list

  DictionaryManager *m_manager;
  QVBoxLayout *m_rows = nullptr;        // one row widget per language
  QLabel *m_status = nullptr;
  QList<DictionaryEntry> m_catalog;
  bool m_catalogLoaded = false;
  // Per-code Download/Delete button, so a progress update can find its row
  // without rebuilding the whole list mid-download.
  QHash<QString, QPushButton *> m_buttons;
};

#endif // DICTIONARIESSECTION_H
