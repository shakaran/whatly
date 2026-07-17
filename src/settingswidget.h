#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>

#include "permissiondialog.h"
#include "settingsmanager.h"
#include "utils.h"

namespace Ui {
class SettingsWidget;
}

class SettingsWidget : public QWidget {
  Q_OBJECT

signals:
  void updateWindowTheme();
  void updatePageTheme();
  void muteToggled(const bool checked);
  void autoPlayMediaToggled(const bool checked);
  void userAgentChanged(QString userAgentStr);
  void initLock();
  void changeLockPassword();
  void linkedDeviceNameChanged();
  void notificationPopupTimeOutChanged();
  void webTweaksChanged();
  void chatWallpaperChanged();
  void customCssChanged();
  void trayIconChanged();
  void followSystemThemeChanged();
  void chatThemeChanged();
  void privacyBlurChanged();
  void spellCheckChanged();
  void notify(QString message);
  void zoomChanged();
  void zoomMaximizedChanged();
  void appAutoLockChanged();

public:
  explicit SettingsWidget(QWidget *parent = nullptr, int screenNumber = 0,
                          QString engineCachePath = "",
                          QString enginePersistentStoragePath = "");
  ~SettingsWidget();

public slots:
  void refresh();
  void updateDefaultUAButton(const QString engineUA);
  void appLockSetChecked(bool checked);
  void muteAudioSetChecked(bool checked);
  void setCurrentPasswordText(QString str);
  void clearAllData();
  void autoAppLockSetChecked(bool checked);
  void updateAppLockPasswordViewer();
  void appAutoLockingSetChecked(bool checked);
  void toggleTheme();
protected slots:
  bool eventFilter(QObject *obj, QEvent *event);
  void closeEvent(QCloseEvent *event);
  void keyPressEvent(QKeyEvent *e);
private slots:
  QString cachePath();
  QString persistentStoragePath();
  void showSetApplockPasswordDialog();
  bool isChildOf(QObject *Of, QObject *self);
  void applyThemeQuirks();
  void on_appAutoLockcheckBox_toggled(bool checked);
  void on_applock_checkbox_toggled(bool checked);
  void on_autoLockDurationSpinbox_valueChanged(int arg1);
  void on_autoPlayMediaCheckBox_toggled(bool checked);
  void on_automaticThemeCheckBox_toggled(bool checked);
  void on_changeDefaultDownloadLocationPb_clicked();
  void on_chnageCurrentPasswordPushButton_clicked();
  void on_closeButtonActionComboBox_currentIndexChanged(int index);
  void on_defaultUserAgentButton_clicked();
  void on_identifyInLinkedDevicesCheckBox_toggled(bool checked);
  void on_minimizeOnTrayIconClick_toggled(bool checked);
  void on_muteAudioCheckBox_toggled(bool checked);
  void on_dismissEmojiPanelCheckBox_toggled(bool checked);
  void on_languageComboBox_currentIndexChanged(int index);
  void on_notificationCheckBox_toggled(bool checked);
  void on_notificationCombo_currentIndexChanged(int index);
  void on_notificationTimeOutspinBox_valueChanged(int arg1);
  void on_resetAppAutoLockPushButton_clicked();
  void on_setUserAgent_clicked();
  void on_showPermissionsButton_clicked();
  void on_showShortcutsButton_clicked();
  void on_startMinimized_toggled(bool checked);
  void on_styleComboBox_currentTextChanged(const QString &arg1);
  void on_themeComboBox_currentIndexChanged(int index);
  void on_tryNotification_clicked();
  void on_useNativeFileDialog_toggled(bool checked);
  void on_chooseChatWallpaperButton_clicked();
  void on_clearChatWallpaperButton_clicked();
  void on_chooseCustomCssButton_clicked();
  void on_clearCustomCssButton_clicked();
  void on_smoothScrollingCheckBox_toggled(bool checked);
  void on_monochromeTrayIconCheckBox_toggled(bool checked);
  void on_hideTrayIconCheckBox_toggled(bool checked);
  void on_lockOnMinimizeCheckBox_toggled(bool checked);
  void on_followSystemThemeCheckBox_toggled(bool checked);
  void on_chatThemeComboBox_currentIndexChanged(int index);
  void on_privacyBlurComboBox_currentIndexChanged(int index);
  void on_spellCheckCheckBox_toggled(bool checked);
  void on_themeToggleButtonCheckBox_toggled(bool checked);
  void on_privacyBlurButtonCheckBox_toggled(bool checked);
  void on_userAgentLineEdit_editingFinished();
  void on_userAgentLineEdit_textChanged(const QString &arg1);
  void on_viewPassword_clicked();
  void on_zoomMinusMaximized_clicked();
  void on_zoomMinus_clicked();
  void on_zoomPlusMaximized_clicked();
  void on_zoomPlus_clicked();
  void on_zoomResetMaximized_clicked();
  void on_zoomReset_clicked();
  void themeSwitchTimerTimeout();
  void updateAutomaticTheme();

  void on_deletePersistentData_clicked();

private:
  void updateChatWallpaperButtons();
  void updateCustomCssButtons();
  void populateChatThemes();
  void populatePrivacyBlur();
  void populateSpellCheck();
  void updateSpellCheckSummary();
  void saveSpellCheckLanguages();
  // Fills the language picker from the .qm files compiled into the binary, so
  // adding a translation needs no code change.
  void populateLanguages();

  Ui::SettingsWidget *ui;
  QString engineCachePath, enginePersistentStoragePath;
  QTimer *themeSwitchTimer;
};

#endif // SETTINGSWIDGET_H
