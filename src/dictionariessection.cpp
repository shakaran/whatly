#include "dictionariessection.h"
#include "dictionaries.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {
// Human size, matching how the issue lists them ("6.5 MB").
QString humanSize(qint64 bytes) {
  if (bytes <= 0)
    return QString();
  const double mb = double(bytes) / (1024.0 * 1024.0);
  return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}
} // namespace

DictionariesSection::DictionariesSection(DictionaryManager *manager,
                                         QWidget *parent)
    : QWidget(parent), m_manager(manager) {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(6);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  outer->addWidget(m_status);

  m_rows = new QVBoxLayout;
  m_rows->setContentsMargins(0, 0, 0, 0);
  m_rows->setSpacing(4);
  outer->addLayout(m_rows);

  connect(m_manager, &DictionaryManager::catalogReady, this,
          [this](const QList<DictionaryEntry> &entries) {
            m_catalog = entries;
            m_catalogLoaded = true;
            rebuild();
          });
  connect(m_manager, &DictionaryManager::catalogFailed, this,
          [this](const QString &error) {
            // The list could not be fetched. What is already installed is still
            // usable, so show those and let the user retry the online list.
            m_catalogLoaded = true;
            rebuild();
            setStatus(tr("Could not fetch the list of downloadable "
                         "dictionaries: %1").arg(error));
          });
  connect(m_manager, &DictionaryManager::downloadProgress, this,
          [this](const QString &code, int percent) {
            if (auto *b = m_buttons.value(code))
              b->setText(tr("%1%").arg(percent));
          });
  connect(m_manager, &DictionaryManager::downloadFinished, this,
          [this](const QString &code, bool ok, const QString &error) {
            rebuild();
            if (ok)
              emit installedChanged();
            else
              setStatus(tr("Downloading %1 failed: %2")
                            .arg(DictionaryManager::displayName(code), error));
          });

  setStatus(tr("Loading available dictionaries…"));
  // The owner (SettingsWidget) triggers the shared fetch once both this section
  // and the language picker are wired to the manager.
}

void DictionariesSection::setStatus(const QString &text) {
  m_status->setText(text);
  m_status->setVisible(!text.isEmpty());
}

void DictionariesSection::rebuild() {
  m_buttons.clear();
  // Clear existing rows.
  QLayoutItem *item;
  while ((item = m_rows->takeAt(0))) {
    if (QWidget *w = item->widget())
      w->deleteLater();
    delete item;
  }

  const QStringList installedList = Dictionaries::availableDictionaries();
  const QSet<QString> installed(installedList.cbegin(), installedList.cend());

  // The rows: every catalogued language, plus anything installed that the
  // catalogue does not list (a bundled extra, or a user-dropped .bdic), so the
  // list always accounts for what is on disk.
  QList<DictionaryEntry> rows = m_catalog;
  QSet<QString> known;
  for (const DictionaryEntry &e : rows)
    known.insert(e.code);
  for (const QString &code : installed)
    if (!known.contains(code)) {
      DictionaryEntry e;
      e.code = code;
      rows.append(e);
    }

  std::sort(rows.begin(), rows.end(),
            [](const DictionaryEntry &a, const DictionaryEntry &b) {
              return DictionaryManager::displayName(a.code).localeAwareCompare(
                         DictionaryManager::displayName(b.code)) < 0;
            });

  if (m_catalogLoaded)
    setStatus(rows.isEmpty() ? tr("No dictionaries are available.") : QString());

  for (const DictionaryEntry &e : rows) {
    auto *row = new QWidget(this);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);

    auto *name = new QLabel(
        QStringLiteral("%1 (%2)").arg(DictionaryManager::displayName(e.code),
                                      e.code),
        row);
    h->addWidget(name);
    h->addStretch(1);

    const bool have = installed.contains(e.code);
    auto *info = new QLabel(row);
    h->addWidget(info);
    auto *btn = new QPushButton(row);
    h->addWidget(btn);
    m_buttons.insert(e.code, btn);

    if (have) {
      if (DictionaryManager::isRemovable(e.code)) {
        info->setText(tr("installed"));
        btn->setText(tr("Delete"));
        connect(btn, &QPushButton::clicked, this, [this, code = e.code]() {
          if (m_manager->remove(code)) {
            rebuild();
            emit installedChanged();
          }
        });
      } else {
        // A bundled dictionary is a symlink into the read-only bundle; deleting
        // it would just relink next launch, so it has no Delete button.
        info->setText(tr("installed (bundled)"));
        btn->setVisible(false);
      }
    } else {
      info->setText(humanSize(e.size));
      btn->setText(tr("Download"));
      // No sha256 means the manifest could not vouch for it; do not offer it,
      // since a download that cannot be verified is not trusted.
      btn->setEnabled(!e.sha256.isEmpty());
      connect(btn, &QPushButton::clicked, this, [this, e, btn]() {
        btn->setEnabled(false);
        setStatus(QString());
        m_manager->download(e);
      });
    }
    m_rows->addWidget(row);
  }
}
