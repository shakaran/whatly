#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QElapsedTimer>
#include <QHash>
#include <QWidget>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QStandardItemModel;
class QTime;
class QToolButton;

#include "permissiondialog.h"
#include "settingsmanager.h"
#include "utils.h"
#include "dictionarymanager.h"
#include "settingssearch.h"

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
  void customJsChanged();
  void focusModeChanged();
  void hdMediaChanged();
  void undoSendChanged();
  void trayIconChanged();
  void followSystemThemeChanged();
  void chatThemeChanged();
  void privacyBlurChanged();
  void chatListStripChanged();
  void fontChanged();
  void mutedStatusChanged();
  void spellCheckChanged();
  void notify(QString message);
  void zoomChanged();
  void zoomMaximizedChanged();
  void appAutoLockChanged();
  // Emitted when the local API / webhook settings change, so the window can
  // (re)start or stop the server.
  void localApiSettingsChanged();
  // "Restart now": the settings that only take effect at launch are worth
  // nothing until the app comes back, so offer to do it here.
  void restartRequested();

public:
  explicit SettingsWidget(QWidget *parent = nullptr, int screenNumber = 0,
                          QString engineCachePath = "",
                          QString enginePersistentStoragePath = "");
  ~SettingsWidget();

public slots:
  void refresh();
  // Remember/replay everything about how this page is left: which sections are
  // open, how far it is scrolled, where the window sits, and whether it was
  // open at all — so a restart can put it back rather than merely reopening it.
  void saveUiState();
  void restoreUiState();
  void updateDefaultUAButton(const QString engineUA);
  void appLockSetChecked(bool checked);
  void muteAudioSetChecked(bool checked);
  void setCurrentPasswordText(QString str);
  void clearAllData();
  void autoAppLockSetChecked(bool checked);
  void updateAppLockPasswordViewer();
  void appAutoLockingSetChecked(bool checked);
  void toggleTheme();
  // What the language box says about the spell checker. Public because the focused
  // language can be changed from outside this window — the tray menu and the
  // keyboard both do it — and an open Settings page went on showing the count it
  // had ("3 languages") while one of those three was doing the work.
  void updateSpellCheckSummary();
protected slots:
  bool eventFilter(QObject *obj, QEvent *event);
  void closeEvent(QCloseEvent *event);
  void resizeEvent(QResizeEvent *event) override;
  // Long tooltips arrive from the .ui as a single line, and Qt does not wrap
  // plain-text tooltips — so a sentence-long one stretches across the whole
  // screen. Re-wrap them to about two thirds of this window's width. That is also
  // what stops them being clipped: a tooltip that narrow no longer reaches the
  // screen edge for Qt to have to shove back on-screen.
  void wrapLongTooltips();
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
  void on_minimizeOnlyFocusedWindowCheckBox_toggled(bool checked);
  void on_muteAudioCheckBox_toggled(bool checked);
  void on_dismissEmojiPanelCheckBox_toggled(bool checked);
  void on_languageComboBox_currentIndexChanged(int index);
  void on_notificationCheckBox_toggled(bool checked);
  void on_notificationSoundCheckBox_toggled(bool checked);
  void on_notificationCombo_currentIndexChanged(int index);
  void on_notificationTimeOutspinBox_valueChanged(int arg1);
  void on_resetAppAutoLockPushButton_clicked();
  void on_setUserAgent_clicked();
  void on_showPermissionsButton_clicked();
  void on_showShortcutsButton_clicked();
  void on_startMinimized_toggled(bool checked);
  void on_rememberWindowLayoutCheckBox_toggled(bool checked);
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
  void on_lockOnScreenLockCheckBox_toggled(bool checked);
  void on_followSystemThemeCheckBox_toggled(bool checked);
  void on_chatThemeComboBox_currentIndexChanged(int index);
  void on_privacyBlurComboBox_currentIndexChanged(int index);
  void on_fontFamilyComboBox_currentIndexChanged(int index);
  void on_hideMutedStatusCheckBox_toggled(bool checked);
  void on_autoRestartCheckBox_toggled(bool checked);
  void on_interfaceFontSizeSpinBox_valueChanged(int arg1);
  void on_disableGpuCheckBox_toggled(bool checked);
  void on_disableGpuCompositingCheckBox_toggled(bool checked);
  void on_disableGpuVsyncCheckBox_toggled(bool checked);
  void on_inProcessGpuCheckBox_toggled(bool checked);
  void on_ignoreGpuBlocklistCheckBox_toggled(bool checked);
  void on_singleProcessCheckBox_toggled(bool checked);
  void on_processPerSiteCheckBox_toggled(bool checked);
  void on_optimizeForSizeCheckBox_toggled(bool checked);
  void on_webrtcShieldCheckBox_toggled(bool checked);
  void on_focusModeCheckBox_toggled(bool checked);
  void on_hdMediaCheckBox_toggled(bool checked);
  void on_undoSendCheckBox_toggled(bool checked);
  void on_undoSendSecondsSpinBox_valueChanged(int arg1);
  void on_translateEnabledCheckBox_toggled(bool checked);
  void on_translateEndpointLineEdit_editingFinished();
  void on_translateApiKeyLineEdit_editingFinished();
  void on_translateTargetLineEdit_editingFinished();
  void on_aiEnabledCheckBox_toggled(bool checked);
  void on_aiEndpointLineEdit_editingFinished();
  void on_aiModelLineEdit_editingFinished();
  void on_aiApiKeyLineEdit_editingFinished();
  void on_aiDetectButton_clicked();
  void on_aiInstalledModelsCombo_activated(int index);
  void on_aiDownloadButton_clicked();
  void on_jsMemoryLimitSpinBox_valueChanged(int arg1);
  void on_cacheTypeComboBox_currentIndexChanged(int index);
  void on_fontHintingComboBox_currentIndexChanged(int index);
  void on_suspendInactiveAccountsCheckBox_toggled(bool checked);
  void on_suspendAfterSpinBox_valueChanged(int arg1);
  void on_unloadOffscreenWindowsCheckBox_toggled(bool checked);
  void on_cacheMaxSpinBox_valueChanged(int arg1);
  void on_autostartCheckBox_toggled(bool checked);
  void on_customWindowFrameCheckBox_toggled(bool checked);
  void on_restartNowButton_clicked();
  void on_tabsInTitleBarCheckBox_toggled(bool checked);
  void on_alwaysShowAccountTabsCheckBox_toggled(bool checked);
  void on_checkUpdatesCheckBox_toggled(bool checked);
  void on_clearCacheButton_clicked();
  void on_exportProfileButton_clicked();
  void on_importProfileButton_clicked();
  void on_interfaceScaleSpinBox_valueChanged(double arg1);
  void on_proxyModeComboBox_currentIndexChanged(int index);
  void on_proxyHostLineEdit_editingFinished();
  void on_cloudPhoneIdEdit_editingFinished();
  void on_cloudTokenEdit_editingFinished();
  void on_cloudApiVersionEdit_editingFinished();
  void on_localApiEnabledCheckBox_toggled(bool checked);
  void on_localApiPortSpinBox_valueChanged(int value);
  void on_localApiTokenEdit_editingFinished();
  void on_webhookEnabledCheckBox_toggled(bool checked);
  void on_webhookVerifyTokenEdit_editingFinished();
  void on_webhookAppSecretEdit_editingFinished();
  void on_proxyPortSpinBox_valueChanged(int arg1);
  void on_proxyUserLineEdit_editingFinished();
  void on_proxyPasswordLineEdit_editingFinished();
  void on_notificationBackendComboBox_currentIndexChanged(int index);
  void on_dndCheckBox_toggled(bool checked);
  void on_dndStartTimeEdit_timeChanged(const QTime &t);
  void on_dndEndTimeEdit_timeChanged(const QTime &t);
  void on_keywordsLineEdit_editingFinished();
  void on_vipContactsLineEdit_editingFinished();
  void on_mutedContactsLineEdit_editingFinished();
  void on_inlineReplyCheckBox_toggled(bool checked);
  void on_addCannedButton_clicked();
  void on_removeCannedButton_clicked();
  void on_addJsAddonButton_clicked();
  void on_removeJsAddonButton_clicked();
  void on_jsAddonsList_itemChanged(QListWidgetItem *item);
  void on_spellCheckCheckBox_toggled(bool checked);
  void on_themeToggleButtonCheckBox_toggled(bool checked);
  void on_privacyBlurButtonCheckBox_toggled(bool checked);
  void on_zoomButtonsCheckBox_toggled(bool checked);
  void on_chatListStripButtonCheckBox_toggled(bool checked);
  void on_chatListPreviewSizeComboBox_currentIndexChanged(int index);
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
  // Keep the two window-frame checkboxes telling the truth about each other:
  // hiding the title bar switches the custom frame on, and dropping the custom
  // frame means the title bar is back whatever the other box remembers.
  void updateTitleBarOptionState();
  void loadPerformanceSettings();
  void loadNetworkSettings();
  void loadCloudApiSettings();
  void loadLocalApiSettings();
  void loadNotificationRules();
  void loadShortcuts();
  void refreshJsAddonsList();
  void refreshCannedList();
  void updateChatWallpaperButtons();
  void updateCustomCssButtons();
  void populateChatThemes();
  void populatePrivacyBlur();
  void populateChatListPreviewSize();
  void populateFontFamilies();
  void populateSpellCheck();
  void saveSpellCheckLanguages();
  // The picker's rows: every language, ticked or not, here or downloadable. Kept in
  // step with what is on disk rather than rebuilt, because the list is usually open
  // while it changes. See DictionaryRows.
  void syncSpellCheckRows();
  void setSpellCheckRowProgress(const QString &code, int progress,
                                const QString &error = QString());
  void fetchDictionaryCatalog();                // the downloadable languages
  void keepTheSpinnersTurning();                // repaint while a download runs
  void downloadDictionary(const QString &code); // the row's arrow
  void deleteDictionary(const QString &code);   // the row's bin
  // Fills the language picker from the .qm files compiled into the binary, so
  // adding a translation needs no code change.
  void populateLanguages();
  // Whether a wheel over this widget has content of its own to scroll in that
  // direction. The page's own scroll area does not count — scrolling the page is
  // what the caller falls back to.
  bool hasScrollOfItsOwn(QWidget *target, int angleDeltaY) const;

  // ── Searching the page (issue #39) ────────────────────────────────────────
  // The box goes in the header, beside the title, so it stays put while the page
  // scrolls under it.
  void buildSearchBox();
  // Section by section, row by row, worked out once and kept: the accordion is
  // assembled in the constructor and nothing moves afterwards.
  void buildSearchIndex();
  // Show what matches, hide the rest, and open every section that has something
  // in it. An empty query puts the page back exactly as it was found, accordion
  // included.
  void applySearch(const QString &query);
  // Read the form's English back while it is still on screen — after setupUi()
  // and before anything is set from code. With a translation loaded, taking the
  // translators off and calling retranslateUi() puts the English text on the
  // widgets, where it can be harvested and then translated back. That is the only
  // way round: a .qm maps English to the local language and cannot be read the
  // other way, and someone who runs Whatly in Esperanto still knows some of these
  // settings by the English name they were discussed in.
  void harvestEnglishText();
  QHash<const QWidget *, QString> m_englishText;

  // One section of the accordion: its header (whose text is the section title),
  // the group box it opens, and the rows inside it.
  struct SearchSection {
    QToolButton *header = nullptr;
    QWidget *wrapper = nullptr; // header + body, the thing to hide as a whole
    QGroupBox *body = nullptr;
    QString haystack; // the title, in both languages
    QList<SettingsSearch::Row> rows;
  };
  QList<SearchSection> m_searchSections;
  QLineEdit *m_searchBox = nullptr;
  QLabel *m_searchNothing = nullptr;
  // Which sections were open when the search started, so clearing the box gives
  // the page back rather than leaving it wide open.
  QHash<QToolButton *, bool> m_searchWasOpen;
  bool m_searchActive = false;

  Ui::SettingsWidget *ui;
  // The wheel gesture in progress: what the first notch chose to scroll, and how
  // long since the last one. A mouse wheel has no phase to separate one gesture
  // from the next, so a pause is what ends it.
  QElapsedTimer m_wheelIdle;
  bool m_wheelScrollsPage = true;
  QString engineCachePath, enginePersistentStoragePath;
  QTimer *themeSwitchTimer;
  class OllamaManager *m_ollama = nullptr; // lazy; local-model detect/download
  // Downloadable spell-check dictionaries (#46): one catalogue fetch, and the rows
  // of the language picker are what offers them.
  DictionaryManager *m_dictManager = nullptr;
  QList<DictionaryEntry> m_dictCatalog;
  // The picker's own model, kept for the life of the widget so a row can change
  // under an open list. m_spellRowsSyncing marks the writes this refresh does
  // itself, which must not be mistaken for the user ticking something.
  QStandardItemModel *m_spellRows = nullptr;
  bool m_spellRowsSyncing = false;
  // Why there are no downloadable languages, when there are none: kept so the list
  // can say it on a row, and counted so a fetch that keeps failing stops retrying.
  QString m_dictCatalogError;
  int m_dictCatalogTries = 0;
  QTimer *m_spellSpin = nullptr; // repaints the rows that are waiting on a file
};

#endif // SETTINGSWIDGET_H
