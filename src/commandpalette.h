#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QDialog>
#include <QList>
#include <QString>
#include <functional>

class QLineEdit;
class QListWidget;

// A "do anything" quick switcher (Ctrl+K): type to fuzzy-filter a flat list of
// commands — every menu action, "switch to account N", theme toggle, settings —
// and run the highlighted one with Enter. MainWindow builds the command list.
namespace Fuzzy {
// Subsequence fuzzy score of `query` against `text` (case-insensitive). Returns
// -1 when `query` is not a subsequence of `text`; higher is a better match.
// Contiguous runs, word-start hits and earlier matches score higher. An empty
// query matches everything with score 0. Pure and unit-tested.
int score(const QString &query, const QString &text);
} // namespace Fuzzy

class CommandPalette : public QDialog {
  Q_OBJECT
public:
  struct Command {
    QString label;
    std::function<void()> run;
  };

  CommandPalette(QList<Command> commands, QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  // Dismiss the palette when it loses focus, so a click outside it closes it
  // (matching Escape). It is shown non-modal for exactly this reason.
  void changeEvent(QEvent *event) override;

private:
  void refilter();
  void runCurrent();

  QList<Command> m_commands;
  QLineEdit *m_search = nullptr;
  QListWidget *m_list = nullptr;
};

#endif // COMMANDPALETTE_H
