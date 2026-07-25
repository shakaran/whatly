#include "commandpalette.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace Fuzzy {

int score(const QString &query, const QString &text) {
  if (query.isEmpty())
    return 0;
  const QString q = query.toLower();
  const QString t = text.toLower();
  int ti = 0, score = 0, run = 0;
  for (int qi = 0; qi < q.size(); ++qi) {
    const QChar qc = q.at(qi);
    bool found = false;
    while (ti < t.size()) {
      if (t.at(ti) == qc) {
        // Bonuses: contiguous run, and matching at a word start.
        run += 1;
        int bonus = run * 2;
        if (ti == 0 || t.at(ti - 1) == QLatin1Char(' ') ||
            t.at(ti - 1) == QLatin1Char('/'))
          bonus += 5;
        // Earlier matches are slightly better.
        score += bonus + qMax(0, 3 - ti / 4);
        ++ti;
        found = true;
        break;
      }
      run = 0;
      ++ti;
    }
    if (!found)
      return -1; // query is not a subsequence of text
  }
  return score;
}

} // namespace Fuzzy

CommandPalette::CommandPalette(QList<Command> commands, QWidget *parent)
    : QDialog(parent), m_commands(std::move(commands)) {
  setWindowTitle(tr("Command palette"));
  setWindowFlag(Qt::FramelessWindowHint, true);
  // Non-modal on purpose: a modal dialog swallows clicks on the window behind
  // it, so the only way out would be Escape. Non-modal lets those clicks land
  // and deactivate the palette, which closes it (see changeEvent).
  setModal(false);
  resize(520, 360);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Type a command…"));
  m_search->setClearButtonEnabled(true);
  m_search->setAccessibleName(tr("Command search"));
  layout->addWidget(m_search);
  m_list = new QListWidget(this);
  m_list->setAccessibleName(tr("Matching commands"));
  layout->addWidget(m_list);

  connect(m_search, &QLineEdit::textChanged, this, [this]() { refilter(); });
  connect(m_list, &QListWidget::itemActivated, this,
          [this]() { runCurrent(); });

  // Enter runs the selection; Down/Up move into the list from the search box.
  m_search->installEventFilter(this);
  refilter();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_search && event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(event);
    switch (ke->key()) {
    case Qt::Key_Down:
      if (m_list->count())
        m_list->setCurrentRow(qMin(m_list->currentRow() + 1, m_list->count() - 1));
      return true;
    case Qt::Key_Up:
      if (m_list->count())
        m_list->setCurrentRow(qMax(m_list->currentRow() - 1, 0));
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      runCurrent();
      return true;
    default:
      break;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void CommandPalette::changeEvent(QEvent *event) {
  // Losing window activation means the user clicked outside the palette (or
  // switched away) — dismiss it, just like Escape.
  if (event->type() == QEvent::ActivationChange && !isActiveWindow())
    close();
  QDialog::changeEvent(event);
}

void CommandPalette::refilter() {
  const QString q = m_search ? m_search->text().trimmed() : QString();
  m_list->clear();
  QList<QPair<int, int>> ranked; // (score, index)
  for (int i = 0; i < m_commands.size(); ++i) {
    const int s = Fuzzy::score(q, m_commands[i].label);
    if (s >= 0)
      ranked.append({s, i});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](auto a, auto b) { return a.first > b.first; });
  for (const auto &r : ranked) {
    auto *item = new QListWidgetItem(m_commands[r.second].label, m_list);
    item->setData(Qt::UserRole, r.second);
  }
  if (m_list->count() > 0)
    m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent() {
  auto *item = m_list->currentItem();
  if (!item)
    return;
  const int idx = item->data(Qt::UserRole).toInt();
  accept();
  if (idx >= 0 && idx < m_commands.size() && m_commands[idx].run)
    m_commands[idx].run();
}
