#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <QWizard>

class QCheckBox;
class QComboBox;

// A short first-run wizard: a welcome, a couple of sensible starting choices
// (start at login, follow the system theme, and — on Linux — how notifications
// are delivered), and a finish page. Everything it sets is also reachable later
// from Settings, so it is purely a friendlier on-ramp; skipping it changes
// nothing. It writes only through SettingsManager / Performance / Autostart, so
// it has no dependency on MainWindow and can be constructed and tested in
// isolation.
class SetupWizard : public QWizard {
  Q_OBJECT
public:
  explicit SetupWizard(QWidget *parent = nullptr);

  // Whether the wizard has already been completed for this account.
  static bool isCompleted();
  // Mark it completed (also called automatically when the wizard is accepted).
  static void markCompleted();

  // Persist the chosen options. Public so a test can drive it without exec().
  void applyChoices();

private:
  QCheckBox *m_autostart = nullptr;
  QCheckBox *m_followSystemTheme = nullptr;
  QComboBox *m_notificationBackend = nullptr;
};

#endif // SETUPWIZARD_H
