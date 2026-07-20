#include "setupwizard.h"
#include "settingsmanager.h"
#include "autostart.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {
const char kCompletedKey[] = "setupWizardCompleted";

QWizardPage *makePage(const QString &title, const QString &subtitle) {
  auto *page = new QWizardPage;
  page->setTitle(title);
  if (!subtitle.isEmpty())
    page->setSubTitle(subtitle);
  return page;
}
} // namespace

SetupWizard::SetupWizard(QWidget *parent) : QWizard(parent) {
  setWindowTitle(tr("Welcome to Whatly"));
  setWizardStyle(QWizard::ModernStyle);
  setOption(QWizard::NoBackButtonOnStartPage, true);

  // ── Welcome ────────────────────────────────────────────────────────────────
  {
    auto *page = makePage(tr("Welcome to Whatly"),
                          tr("A fast, native WhatsApp Web client for the "
                             "desktop. Let's set a few things up — you can "
                             "change all of this later in Settings."));
    auto *layout = new QVBoxLayout(page);
    auto *body = new QLabel(
        tr("Whatly keeps your chats in a proper desktop window, with a tray "
           "icon, notifications, themes, and multiple accounts."),
        page);
    body->setWordWrap(true);
    layout->addWidget(body);
    addPage(page);
  }

  // ── Preferences ──────────────────────────────────────────────────────────
  {
    auto *page = makePage(tr("A few preferences"),
                          tr("Sensible defaults; tweak to taste."));
    auto *layout = new QVBoxLayout(page);

    m_followSystemTheme = new QCheckBox(tr("Match the system light/dark theme"),
                                        page);
    m_followSystemTheme->setChecked(true);
    layout->addWidget(m_followSystemTheme);

    m_autostart = new QCheckBox(tr("Start Whatly when I log in"), page);
    m_autostart->setChecked(false);
    m_autostart->setVisible(Autostart::isSupported());
    layout->addWidget(m_autostart);

#ifdef Q_OS_LINUX
    auto *notifRow = new QLabel(tr("Notification delivery:"), page);
    layout->addWidget(notifRow);
    m_notificationBackend = new QComboBox(page);
    m_notificationBackend->addItem(tr("Automatic"), QStringLiteral("auto"));
    m_notificationBackend->addItem(tr("Desktop portal (Flatpak)"),
                                   QStringLiteral("portal"));
    m_notificationBackend->addItem(tr("System service (libnotify)"),
                                   QStringLiteral("libnotify"));
    layout->addWidget(m_notificationBackend);
#endif
    layout->addStretch();
    addPage(page);
  }

  // ── Finish ─────────────────────────────────────────────────────────────────
  {
    auto *page = makePage(tr("All set"),
                          tr("You're ready to go. Scan the QR code with your "
                             "phone to sign in."));
    auto *layout = new QVBoxLayout(page);
    auto *body = new QLabel(
        tr("Everything here lives in Settings if you change your mind."), page);
    body->setWordWrap(true);
    layout->addWidget(body);
    addPage(page);
  }

  connect(this, &QDialog::accepted, this, [this]() { applyChoices(); });
}

void SetupWizard::applyChoices() {
  QSettings &s = SettingsManager::instance().settings();
  if (m_followSystemTheme)
    s.setValue("followSystemTheme", m_followSystemTheme->isChecked());
  if (m_autostart && Autostart::isSupported())
    Autostart::setEnabled(m_autostart->isChecked());
  if (m_notificationBackend)
    s.setValue("notificationBackend",
               m_notificationBackend->currentData().toString());
  markCompleted();
}

bool SetupWizard::isCompleted() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kCompletedKey), false)
      .toBool();
}

void SetupWizard::markCompleted() {
  SettingsManager::instance().settings().setValue(QLatin1String(kCompletedKey),
                                                  true);
}
