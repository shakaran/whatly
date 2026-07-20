#include "settingswidget.h"
#include "ui_settingswidget.h"

#include "mainwindow.h"
#include <QDateTime>
#include <QFileDialog>
#include <QImageReader>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>
#include <QLocale>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCheckBox>

#include "automatictheme.h"
#include "chattheme.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "webengineprofilemanager.h"
#include "dictionaries.h"
#include "privacyblur.h"
#include "webfont.h"
#include "mutedstatus.h"
#include "performance.h"
#include "networkproxy.h"
#include "autostart.h"
#include "customjs.h"
#include "customtitlebar.h"

#include <QListWidget>

// The theme combo's two entries, in .ui order. The stored value is derived
// from these, never from the item text — which is translated.
static const int kThemeIndexDark = 0;
static const int kThemeIndexLight = 1;

static int themeIndexFromSettings() {
  return SettingsManager::instance()
                     .settings()
                     .value("windowTheme", "light")
                     .toString() == QLatin1String("dark")
             ? kThemeIndexDark
             : kThemeIndexLight;
}

extern QString defaultUserAgentStr;
extern int defaultAppAutoLockDuration;
extern bool defaultAppAutoLock;
extern double defaultZoomFactorMaximized;

SettingsWidget::SettingsWidget(QWidget *parent, int screenNumber,
                               QString engineCachePath,
                               QString enginePersistentStoragePath)
    : QWidget(parent), ui(new Ui::SettingsWidget) {
  ui->setupUi(this);

  this->engineCachePath = engineCachePath;
  this->enginePersistentStoragePath = enginePersistentStoragePath;

  ui->zoomFactorSpinBox->setRange(0.25, 5.0);
  ui->zoomFactorSpinBox->setValue(SettingsManager::instance()
                                      .settings()
                                      .value("zoomFactor", 1.0)
                                      .toDouble());

  ui->zoomFactorSpinBoxMaximized->setRange(0.25, 5.0);
  ui->zoomFactorSpinBoxMaximized->setValue(
      SettingsManager::instance()
          .settings()
          .value("zoomFactorMaximized", defaultZoomFactorMaximized)
          .toDouble());

  ui->closeButtonActionComboBox->setCurrentIndex(
      SettingsManager::instance()
          .settings()
          .value("closeButtonActionCombo", 0)
          .toInt());
  ui->notificationCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("disableNotificationPopups", false)
          .toBool());
  ui->muteAudioCheckBox->setChecked(SettingsManager::instance()
                                        .settings()
                                        .value("muteAudio", false)
                                        .toBool());
  ui->autoPlayMediaCheckBox->setChecked(SettingsManager::instance()
                                            .settings()
                                            .value("autoPlayMedia", false)
                                            .toBool());
  // By index, never by text: the items are translated, and keying the stored
  // theme on what they happen to say in the current language is what used to
  // write windowTheme=claro and leave the app permanently unable to go dark.
  ui->themeComboBox->setCurrentIndex(themeIndexFromSettings());

  ui->userAgentLineEdit->setText(SettingsManager::instance()
                                     .settings()
                                     .value("useragent", defaultUserAgentStr)
                                     .toString());
  ui->userAgentLineEdit->home(true);
  ui->userAgentLineEdit->deselect();

  ui->notificationTimeOutspinBox->setValue(
      SettingsManager::instance()
          .settings()
          .value("notificationTimeOut", 9000)
          .toInt() /
      1000);
  ui->notificationCombo->setCurrentIndex(SettingsManager::instance()
                                             .settings()
                                             .value("notificationCombo", 0)
                                             .toInt());
  ui->useNativeFileDialog->setChecked(SettingsManager::instance()
                                          .settings()
                                          .value("useNativeFileDialog", true)
                                          .toBool());
  ui->startMinimized->setChecked(SettingsManager::instance()
                                     .settings()
                                     .value("startMinimized", false)
                                     .toBool());
  ui->dismissEmojiPanelCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("webtweaks/dismissExpressionsPanel", false)
          .toBool());
  ui->themeToggleButtonCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("webtweaks/themeToggleButton", true)
          .toBool());
  ui->privacyBlurButtonCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("webtweaks/privacyBlurButton", true)
          .toBool());
  ui->identifyInLinkedDevicesCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("identifyInLinkedDevices", true)
          .toBool());
  populateLanguages();
  populateChatThemes();
  populatePrivacyBlur();
  populateFontFamilies();
  populateSpellCheck();
  updateCustomCssButtons();
  ui->smoothScrollingCheckBox->setChecked(
      SettingsManager::instance().settings().value("smoothScrolling", false).toBool());
  ui->monochromeTrayIconCheckBox->setChecked(
      SettingsManager::instance().settings().value("monochromeTrayIcon", false).toBool());
  ui->hideTrayIconCheckBox->setChecked(
      SettingsManager::instance().settings().value("hideTrayIcon", false).toBool());
  ui->hideMutedStatusCheckBox->setChecked(MutedStatus::isEnabled());
  ui->autoRestartCheckBox->setChecked(
      SettingsManager::instance().settings().value("autoRestartOnCrash", false).toBool());
  ui->interfaceFontSizeSpinBox->blockSignals(true);
  ui->interfaceFontSizeSpinBox->setValue(
      SettingsManager::instance()
          .settings()
          .value("interfaceFontSize", qApp->font().pointSize())
          .toInt());
  ui->interfaceFontSizeSpinBox->blockSignals(false);
  loadPerformanceSettings();
  loadNetworkSettings();
  refreshJsAddonsList();
  ui->lockOnMinimizeCheckBox->setChecked(
      SettingsManager::instance().settings().value("lockOnHideToTray", false).toBool());
  {
    const bool followSystem =
        SettingsManager::instance().settings().value("followSystemTheme", false).toBool();
    ui->followSystemThemeCheckBox->blockSignals(true);
    ui->followSystemThemeCheckBox->setChecked(followSystem);
    ui->followSystemThemeCheckBox->blockSignals(false);
    ui->themeComboBox->setEnabled(!followSystem);
    ui->automaticThemeCheckBox->setEnabled(!followSystem);
  }
  updateChatWallpaperButtons();

  this->appAutoLockingSetChecked(
      SettingsManager::instance()
          .settings()
          .value("appAutoLocking", defaultAppAutoLock)
          .toBool());

  ui->autoLockDurationSpinbox->setValue(
      SettingsManager::instance()
          .settings()
          .value("autoLockDuration", defaultAppAutoLockDuration)
          .toInt());
  ui->minimizeOnTrayIconClick->setChecked(
      SettingsManager::instance()
          .settings()
          .value("minimizeOnTrayIconClick", false)
          .toBool());
  ui->defaultDownloadLocation->setText(
      SettingsManager::instance()
          .settings()
          .value("defaultDownloadLocation",
                 QStandardPaths::writableLocation(
                     QStandardPaths::DownloadLocation) +
                     QDir::separator() + QApplication::applicationDisplayName())
          .toString());

  ui->styleComboBox->blockSignals(true);
  ui->styleComboBox->addItems(QStyleFactory::keys());
  ui->styleComboBox->blockSignals(false);
  ui->styleComboBox->setCurrentText(SettingsManager::instance()
                                        .settings()
                                        .value("widgetStyle", "Fusion")
                                        .toString());

  ui->automaticThemeCheckBox->blockSignals(true);
  bool automaticThemeSwitching = SettingsManager::instance()
                                     .settings()
                                     .value("automaticTheme", false)
                                     .toBool();
  ui->automaticThemeCheckBox->setChecked(automaticThemeSwitching);
  ui->automaticThemeCheckBox->blockSignals(false);

  themeSwitchTimer = new QTimer(this);
  themeSwitchTimer->setInterval(60000); // 1 min
  connect(themeSwitchTimer, &QTimer::timeout, this,
          [=]() { themeSwitchTimerTimeout(); });

  // instantly call the timeout slot if automatic theme switching enabled
  if (automaticThemeSwitching)
    themeSwitchTimerTimeout();
  // start regular timer to update theme
  updateAutomaticTheme();

  this->setCurrentPasswordText(
      QByteArray::fromBase64(SettingsManager::instance()
                                 .settings()
                                 .value("asdfg")
                                 .toString()
                                 .toUtf8()));

  applyThemeQuirks();

  ui->setUserAgent->setEnabled(false);

  // event filter to prevent wheel event on certain widgets
  foreach (QSlider *slider, this->findChildren<QSlider *>()) {
    slider->installEventFilter(this);
  }
  foreach (QComboBox *box, this->findChildren<QComboBox *>()) {
    box->installEventFilter(this);
  }
  foreach (QSpinBox *spinBox, this->findChildren<QSpinBox *>()) {
    spinBox->installEventFilter(this);
  }

  ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  this->setMinimumHeight(580);

  ui->scrollArea->setMinimumWidth(
      ui->groupBox_8->sizeHint().width() + ui->scrollArea->sizeHint().width() +
      ui->scrollAreaWidgetContents->layout()->spacing());
  if (SettingsManager::instance().settings().value("settingsGeo").isValid()) {
    this->restoreGeometry(SettingsManager::instance()
                              .settings()
                              .value("settingsGeo")
                              .toByteArray());
    QRect screenRect = QGuiApplication::screens().at(screenNumber)->geometry();
    if (!screenRect.contains(this->pos())) {
      this->move(screenRect.center() - this->rect().center());
    }
  }
}

bool SettingsWidget::eventFilter(QObject *obj, QEvent *event) {

  // The spell-check language combo is a multi-select: a click on the drop-down
  // list toggles that language's checkbox and keeps the list open, instead of
  // picking one entry and closing (which is what a plain combo does).
  if (ui->spellCheckLanguageComboBox->view() &&
      obj == ui->spellCheckLanguageComboBox->view()->viewport() &&
      event->type() == QEvent::MouseButtonRelease) {
    auto *me = static_cast<QMouseEvent *>(event);
    QAbstractItemView *view = ui->spellCheckLanguageComboBox->view();
    const QModelIndex index = view->indexAt(me->pos());
    if (index.isValid()) {
      const Qt::CheckState now =
          view->model()->data(index, Qt::CheckStateRole).value<Qt::CheckState>();
      view->model()->setData(index, now == Qt::Checked ? Qt::Unchecked
                                                        : Qt::Checked,
                             Qt::CheckStateRole);
    }
    return true; // consume, so the popup does not close
  }

  if (isChildOf(this, obj)) {
    if (event->type() == QEvent::Wheel) {
      return true;
    }
  }
  return QWidget::eventFilter(obj, event);
}

void SettingsWidget::closeEvent(QCloseEvent *event) {
  SettingsManager::instance().settings().setValue("settingsGeo",
                                                  this->saveGeometry());
  QWidget::closeEvent(event);
}

bool SettingsWidget::isChildOf(QObject *Of, QObject *self) {
  bool ischild = false;
  if (Of->findChild<QWidget *>(self->objectName())) {
    ischild = true;
  }
  return ischild;
}

inline bool inRange(unsigned low, unsigned high, unsigned x) {
  return ((x - low) <= (high - low));
}

void SettingsWidget::themeSwitchTimerTimeout() {
  if (SettingsManager::instance()
          .settings()
          .value("automaticTheme", false)
          .toBool()) {
    // start time
    QDateTime sunrise;
    sunrise.setSecsSinceEpoch(
        SettingsManager::instance().settings().value("sunrise").toLongLong());
    // end time
    QDateTime sunset;
    sunset.setSecsSinceEpoch(
        SettingsManager::instance().settings().value("sunset").toLongLong());
    QDateTime currentTime = QDateTime::currentDateTime();

    int sunsetSeconds = QTime(0, 0).secsTo(sunset.time());
    int sunriseSeconds = QTime(0, 0).secsTo(sunrise.time());
    int currentSeconds = QTime(0, 0).secsTo(currentTime.time());

    if (inRange(sunsetSeconds, sunriseSeconds, currentSeconds)) {
      qDebug() << "is night: ";
      ui->themeComboBox->setCurrentIndex(kThemeIndexDark);
    } else {
      qDebug() << "is morn: ";
      ui->themeComboBox->setCurrentIndex(kThemeIndexLight);
    }
  }
}

void SettingsWidget::updateAutomaticTheme() {
  bool automaticThemeSwitching = SettingsManager::instance()
                                     .settings()
                                     .value("automaticTheme", false)
                                     .toBool();
  if (automaticThemeSwitching && !themeSwitchTimer->isActive()) {
    themeSwitchTimer->start();
  } else if (!automaticThemeSwitching) {
    themeSwitchTimer->stop();
  }
}

SettingsWidget::~SettingsWidget() {
  themeSwitchTimer->stop();
  themeSwitchTimer->deleteLater();
  delete ui;
}

void SettingsWidget::refresh() {
  ui->themeComboBox->setCurrentIndex(themeIndexFromSettings());
  populatePrivacyBlur();
  populateFontFamilies();

  ui->cookieSize->setText(Utils::refreshCacheSize(persistentStoragePath()));
}

void SettingsWidget::updateDefaultUAButton(const QString engineUA) {
  bool isDefault =
      QString::compare(engineUA, defaultUserAgentStr, Qt::CaseInsensitive) == 0;
  ui->defaultUserAgentButton->setEnabled(!isDefault);

  if (ui->userAgentLineEdit->text().trimmed().isEmpty()) {
    ui->userAgentLineEdit->setText(engineUA);
  }
}

QString SettingsWidget::cachePath() { return engineCachePath; }

QString SettingsWidget::persistentStoragePath() {
  return enginePersistentStoragePath;
}

void SettingsWidget::on_deletePersistentData_clicked() {
  QMessageBox msgBox;
  msgBox.setText(tr("This will delete Persistent Data ! Persistent data includes "
                 "persistent cookies and Cache, and Quit the application."));
  msgBox.setIconPixmap(
      QPixmap(":/icons/information-line.png")
          .scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  msgBox.setInformativeText(tr("Delete Cookies and Quit Application?"));
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  int ret = msgBox.exec();
  switch (ret) {
  case QMessageBox::Yes: {
    clearAllData();
    qApp->quit();
    break;
  }
  case QMessageBox::No:
    break;
  }
}

void SettingsWidget::clearAllData() {
  Utils::delete_cache(this->cachePath());
  Utils::delete_cache(this->persistentStoragePath());
  refresh();
}

void SettingsWidget::on_notificationCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("disableNotificationPopups",
                                                  checked);
}

void SettingsWidget::on_themeComboBox_currentIndexChanged(int index) {
  applyThemeQuirks();
  SettingsManager::instance().settings().setValue(
      "windowTheme", index == kThemeIndexDark ? QStringLiteral("dark")
                                              : QStringLiteral("light"));
  emit updateWindowTheme();
  emit updatePageTheme();
}

void SettingsWidget::applyThemeQuirks() {
  // little quirks
  if (ui->themeComboBox->currentIndex() == kThemeIndexDark) {
    ui->bottomLine->setStyleSheet("background-color: rgb(0, 117, 96);");
    ui->label_7->setStyleSheet(
        "color:#c2c5d1;padding: 0px 8px 0px 8px;background:transparent;");
    ui->headerWidget->setStyleSheet("background-color: qlineargradient("
                                    "spread:reflect, x1:0, y1:1, x2:1, y2:1,"
                                    "stop:0 rgba(0, 117, 96, 255), "
                                    "stop:0.328358 rgba(0, 117, 96, 144),"
                                    "stop:0.61194 rgba(0, 117, 96, 78),"
                                    "stop:0.895522 rgba(0, 117, 96, 6));");
  } else {
    ui->bottomLine->setStyleSheet("background-color: rgb(37, 211, 102);");
    ui->label_7->setStyleSheet(
        "color:#1e1f21;padding: 0px 8px 0px 8px;background:transparent;");
    ui->headerWidget->setStyleSheet("background-color: qlineargradient("
                                    "spread:reflect, x1:0, y1:1, x2:1, y2:1,"
                                    "stop:0 rgba(37, 211, 102, 200), "
                                    "stop:0.328358 rgba(37, 211, 102, 122),"
                                    "stop:0.61194 rgba(37, 211, 102, 68),"
                                    "stop:0.895522 rgba(37, 211, 102, 6));");
  }
}

void SettingsWidget::on_muteAudioCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("muteAudio", checked);
  emit muteToggled(checked);
}

void SettingsWidget::on_autoPlayMediaCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("autoPlayMedia", checked);
  emit autoPlayMediaToggled(checked);
}

void SettingsWidget::on_defaultUserAgentButton_clicked() {
  ui->userAgentLineEdit->setText(defaultUserAgentStr);
  emit userAgentChanged(ui->userAgentLineEdit->text());
}

void SettingsWidget::on_userAgentLineEdit_textChanged(const QString &arg1) {
  bool isDefault = QString::compare(arg1.trimmed(), defaultUserAgentStr,
                                    Qt::CaseInsensitive) == 0;
  bool isPrevious =
      QString::compare(arg1.trimmed(),
                       SettingsManager::instance()
                           .settings()
                           .value("useragent", defaultUserAgentStr)
                           .toString(),
                       Qt::CaseInsensitive) == 0;

  if (isDefault == false && arg1.trimmed().isEmpty() == false) {
    ui->defaultUserAgentButton->setEnabled(false);
    ui->setUserAgent->setEnabled(false);
  }
  if (isPrevious == false && arg1.trimmed().isEmpty() == false) {
    ui->setUserAgent->setEnabled(true);
    ui->defaultUserAgentButton->setEnabled(true);
  }
  if (isPrevious) {
    ui->defaultUserAgentButton->setEnabled(true);
    ui->setUserAgent->setEnabled(false);
  }
}

void SettingsWidget::on_setUserAgent_clicked() {
  if (ui->userAgentLineEdit->text().trimmed().isEmpty()) {
    QMessageBox::information(this, QApplication::applicationDisplayName() + tr("| Error"),
                             tr("Cannot set an empty UserAgent String."));
    return;
  }
  emit userAgentChanged(ui->userAgentLineEdit->text());
}

void SettingsWidget::on_closeButtonActionComboBox_currentIndexChanged(
    int index) {
  SettingsManager::instance().settings().setValue("closeButtonActionCombo",
                                                  index);
}

void SettingsWidget::autoAppLockSetChecked(bool checked) {
  ui->appAutoLockcheckBox->blockSignals(true);
  ui->appAutoLockcheckBox->setChecked(checked);
  ui->appAutoLockcheckBox->blockSignals(false);
}

void SettingsWidget::updateAppLockPasswordViewer() {
  this->setCurrentPasswordText(
      QByteArray::fromBase64(SettingsManager::instance()
                                 .settings()
                                 .value("asdfg")
                                 .toString()
                                 .toUtf8()));
}

void SettingsWidget::muteAudioSetChecked(bool checked) {
  ui->muteAudioCheckBox->blockSignals(true);
  ui->muteAudioCheckBox->setChecked(checked);
  ui->muteAudioCheckBox->blockSignals(false);
}

void SettingsWidget::appLockSetChecked(bool checked) {
  ui->applock_checkbox->blockSignals(true);
  ui->applock_checkbox->setChecked(checked);
  ui->applock_checkbox->blockSignals(false);
}

void SettingsWidget::appAutoLockingSetChecked(bool checked) {
  ui->appAutoLockcheckBox->blockSignals(true);
  ui->appAutoLockcheckBox->setChecked(checked);
  ui->appAutoLockcheckBox->blockSignals(false);
}

void SettingsWidget::toggleTheme() {
  // disable automatic theme first
  if (SettingsManager::instance()
          .settings()
          .value("automaticTheme", false)
          .toBool()) {
    emit notify(tr(
        "Automatic theme switching was disabled due to manual theme toggle."));
    ui->automaticThemeCheckBox->setChecked(false);
  }
  if (ui->themeComboBox->currentIndex() == 0) {
    ui->themeComboBox->setCurrentIndex(1);
  } else {
    ui->themeComboBox->setCurrentIndex(0);
  }
}

void SettingsWidget::setCurrentPasswordText(QString str) {
  ui->current_password->setStyleSheet(
      "QLineEdit[echoMode=\"2\"]{lineedit-password-character: 9899}");
  if (str == "Require setup") {
    ui->current_password->setEchoMode(QLineEdit::Normal);
  } else {
    ui->current_password->setEchoMode(QLineEdit::Password);
    ui->current_password->setText(str);
  }
}

void SettingsWidget::on_applock_checkbox_toggled(bool checked) {
  if (SettingsManager::instance().settings().value("asdfg").isValid()) {
    SettingsManager::instance().settings().setValue("lockscreen", checked);
  } else if (checked &&
             !SettingsManager::instance().settings().value("asdfg").isValid()) {
    SettingsManager::instance().settings().setValue("lockscreen", true);
    if (checked)
      showSetApplockPasswordDialog();
  } else {
    SettingsManager::instance().settings().setValue("lockscreen", false);
    if (checked)
      showSetApplockPasswordDialog();
  }
}

void SettingsWidget::showSetApplockPasswordDialog() {
  QMessageBox msgBox;
  msgBox.setText(tr("App lock is not configured."));
  msgBox.setIconPixmap(
      QPixmap(":/icons/information-line.png")
          .scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  msgBox.setInformativeText(tr("Do you want to setup App lock now?"));
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
  int ret = msgBox.exec();
  if (ret == QMessageBox::Yes) {
    this->close();
    emit initLock();
  } else {
    ui->applock_checkbox->blockSignals(true);
    ui->applock_checkbox->setChecked(false);
    ui->applock_checkbox->blockSignals(false);
  }
}

void SettingsWidget::on_showShortcutsButton_clicked() {
  QWidget *sheet = new QWidget(this);
  sheet->setWindowTitle(QApplication::applicationDisplayName() +
                        " | Global shortcuts");

  sheet->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
  sheet->move(this->geometry().center() - sheet->geometry().center());

  QVBoxLayout *layout = new QVBoxLayout(sheet);
  sheet->setLayout(layout);
  auto *w = qobject_cast<MainWindow *>(parent());
  if (w != 0) {
    foreach (QAction *action, w->actions()) {
      QString shortcutStr = action->shortcut().toString();
      if (shortcutStr.isEmpty() == false) {
        QLabel *label = new QLabel(
            action->text().remove("&") + "  |  " + shortcutStr, sheet);
        label->setAlignment(Qt::AlignHCenter);
        layout->addWidget(label);
      }
    }
  }
  sheet->setAttribute(Qt::WA_DeleteOnClose);
  sheet->show();
}

void SettingsWidget::on_showPermissionsButton_clicked() {
  PermissionDialog *permissionDialog = new PermissionDialog(this);
  permissionDialog->setWindowTitle(QApplication::applicationDisplayName() + " | " +
                                   tr("Feature permissions"));
  permissionDialog->setWindowFlag(Qt::Dialog);
  permissionDialog->setAttribute(Qt::WA_DeleteOnClose, true);
  permissionDialog->move(this->geometry().center() -
                         permissionDialog->geometry().center());
  // Clamp the minimum to the screen so the dialog still fits on a small display
  // such as a Linux phone (issue #239).
  int pdW = 485, pdH = 310;
  if (QScreen *scr = permissionDialog->screen()) {
    pdW = qMin(pdW, scr->availableSize().width());
    pdH = qMin(pdH, scr->availableSize().height());
  }
  permissionDialog->setMinimumSize(pdW, pdH);
  permissionDialog->adjustSize();
  permissionDialog->show();
}

void SettingsWidget::on_notificationTimeOutspinBox_valueChanged(int arg1) {
  SettingsManager::instance().settings().setValue("notificationTimeOut",
                                                  arg1 * 1000);
  emit notificationPopupTimeOutChanged();
}

void SettingsWidget::on_notificationCombo_currentIndexChanged(int index) {
  SettingsManager::instance().settings().setValue("notificationCombo", index);
}

void SettingsWidget::on_tryNotification_clicked() {
  emit notify("Lorem ipsum dolor sit amet, consectetur adipiscing elit...");
}

void SettingsWidget::on_automaticThemeCheckBox_toggled(bool checked) {
  if (checked) {
    AutomaticTheme *automaticTheme = new AutomaticTheme(this);
    automaticTheme->setWindowTitle(QApplication::applicationDisplayName() +
                                   " | Automatic theme switcher setup");
    automaticTheme->setWindowFlag(Qt::Dialog);
    automaticTheme->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(automaticTheme, &AutomaticTheme::destroyed,
            ui->automaticThemeCheckBox, [=]() {
              bool automaticThemeSwitching = SettingsManager::instance()
                                                 .settings()
                                                 .value("automaticTheme", false)
                                                 .toBool();
              ui->automaticThemeCheckBox->setChecked(automaticThemeSwitching);
              if (automaticThemeSwitching)
                themeSwitchTimerTimeout();
              updateAutomaticTheme();
            });
    automaticTheme->show();
  } else {
    SettingsManager::instance().settings().setValue("automaticTheme", false);
    updateAutomaticTheme();
  }
}

void SettingsWidget::on_useNativeFileDialog_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("useNativeFileDialog",
                                                  checked);
}

void SettingsWidget::on_startMinimized_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("startMinimized", checked);
}

void SettingsWidget::on_dismissEmojiPanelCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/dismissExpressionsPanel", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_chooseChatWallpaperButton_clicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose a chat wallpaper"),
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
      tr("Images (%1)")
          .arg("*." + QImageReader::supportedImageFormats().join(" *.")));
  if (path.isEmpty())
    return;

  QString error;
  if (!ChatWallpaper::setImage(path, &error)) {
    QMessageBox::warning(this, tr("Chat wallpaper"),
                         tr("Could not use that image: %1").arg(error));
    return;
  }
  updateChatWallpaperButtons();
  emit chatWallpaperChanged();
}

void SettingsWidget::on_clearChatWallpaperButton_clicked() {
  ChatWallpaper::clear();
  updateChatWallpaperButtons();
  emit chatWallpaperChanged();
}

void SettingsWidget::on_chooseCustomCssButton_clicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose a CSS file"),
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
      tr("Stylesheets (*.css);;All files (*)"));
  if (path.isEmpty())
    return;

  QString error;
  if (!CustomCss::setFromFile(path, &error)) {
    QMessageBox::warning(this, tr("Custom CSS"),
                         tr("Could not read that file: %1").arg(error));
    return;
  }
  updateCustomCssButtons();
  emit customCssChanged();
}

void SettingsWidget::on_clearCustomCssButton_clicked() {
  CustomCss::clear();
  updateCustomCssButtons();
  emit customCssChanged();
}

void SettingsWidget::updateCustomCssButtons() {
  ui->clearCustomCssButton->setEnabled(CustomCss::isActive());
}

void SettingsWidget::on_followSystemThemeCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("followSystemTheme", checked);
  // When enabled, the manual theme combo and the sunrise/sunset switch no longer
  // decide the theme, so grey them out to say so.
  ui->themeComboBox->setEnabled(!checked);
  ui->automaticThemeCheckBox->setEnabled(!checked);
  emit followSystemThemeChanged();
}

void SettingsWidget::on_hideTrayIconCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("hideTrayIcon", checked);
  emit trayIconChanged();
}

void SettingsWidget::on_hideMutedStatusCheckBox_toggled(bool checked) {
  MutedStatus::setEnabled(checked);
  emit mutedStatusChanged();
}

void SettingsWidget::on_monochromeTrayIconCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("monochromeTrayIcon", checked);
  emit trayIconChanged();
}

void SettingsWidget::on_autoRestartCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("autoRestartOnCrash", checked);
}

void SettingsWidget::on_interfaceFontSizeSpinBox_valueChanged(int arg1) {
  SettingsManager::instance().settings().setValue("interfaceFontSize", arg1);
  // Apply live so the change is visible without a restart.
  QFont f = qApp->font();
  f.setPointSize(arg1);
  qApp->setFont(f);
}

void SettingsWidget::on_lockOnMinimizeCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("lockOnHideToTray", checked);
}

void SettingsWidget::loadPerformanceSettings() {
  // Populate the cache-type combo once (index maps to the stored token).
  if (ui->cacheTypeComboBox->count() == 0) {
    ui->cacheTypeComboBox->blockSignals(true);
    ui->cacheTypeComboBox->addItem(tr("Disk"), QStringLiteral("disk"));
    ui->cacheTypeComboBox->addItem(tr("Memory"), QStringLiteral("memory"));
    ui->cacheTypeComboBox->addItem(tr("None"), QStringLiteral("none"));
    ui->cacheTypeComboBox->blockSignals(false);
  }

  const auto set = [](QCheckBox *box, bool v) {
    box->blockSignals(true);
    box->setChecked(v);
    box->blockSignals(false);
  };
  set(ui->disableGpuCheckBox, Performance::disableGpu());
  set(ui->disableGpuCompositingCheckBox, Performance::disableGpuCompositing());
  set(ui->disableGpuVsyncCheckBox, Performance::disableGpuVsync());
  set(ui->inProcessGpuCheckBox, Performance::inProcessGpu());
  set(ui->ignoreGpuBlocklistCheckBox, Performance::ignoreGpuBlocklist());
  set(ui->singleProcessCheckBox, Performance::singleProcess());
  set(ui->processPerSiteCheckBox, Performance::processPerSite());
  set(ui->webrtcShieldCheckBox, Performance::webrtcShield());

  ui->jsMemoryLimitSpinBox->blockSignals(true);
  ui->jsMemoryLimitSpinBox->setValue(Performance::jsMemoryLimitMb());
  ui->jsMemoryLimitSpinBox->blockSignals(false);

  ui->cacheMaxSpinBox->blockSignals(true);
  ui->cacheMaxSpinBox->setValue(Performance::cacheMaxMb());
  ui->cacheMaxSpinBox->blockSignals(false);

  ui->cacheTypeComboBox->blockSignals(true);
  const int idx = ui->cacheTypeComboBox->findData(Performance::cacheType());
  ui->cacheTypeComboBox->setCurrentIndex(idx < 0 ? 0 : idx);
  ui->cacheTypeComboBox->blockSignals(false);
}

void SettingsWidget::on_disableGpuCheckBox_toggled(bool checked) {
  Performance::setDisableGpu(checked);
}
void SettingsWidget::on_disableGpuCompositingCheckBox_toggled(bool checked) {
  Performance::setDisableGpuCompositing(checked);
}
void SettingsWidget::on_disableGpuVsyncCheckBox_toggled(bool checked) {
  Performance::setDisableGpuVsync(checked);
}
void SettingsWidget::on_inProcessGpuCheckBox_toggled(bool checked) {
  Performance::setInProcessGpu(checked);
}
void SettingsWidget::on_ignoreGpuBlocklistCheckBox_toggled(bool checked) {
  Performance::setIgnoreGpuBlocklist(checked);
}
void SettingsWidget::on_singleProcessCheckBox_toggled(bool checked) {
  Performance::setSingleProcess(checked);
}
void SettingsWidget::on_processPerSiteCheckBox_toggled(bool checked) {
  Performance::setProcessPerSite(checked);
}
void SettingsWidget::on_webrtcShieldCheckBox_toggled(bool checked) {
  Performance::setWebrtcShield(checked);
}
void SettingsWidget::on_jsMemoryLimitSpinBox_valueChanged(int arg1) {
  Performance::setJsMemoryLimitMb(arg1);
}
void SettingsWidget::on_cacheTypeComboBox_currentIndexChanged(int index) {
  Performance::setCacheType(
      ui->cacheTypeComboBox->itemData(index).toString());
}
void SettingsWidget::on_cacheMaxSpinBox_valueChanged(int arg1) {
  Performance::setCacheMaxMb(arg1);
}

void SettingsWidget::loadNetworkSettings() {
  // Autostart: hide the control on platforms where it is not implemented.
  ui->autostartCheckBox->setVisible(Autostart::isSupported());
  if (Autostart::isSupported()) {
    ui->autostartCheckBox->blockSignals(true);
    ui->autostartCheckBox->setChecked(Autostart::isEnabled());
    ui->autostartCheckBox->blockSignals(false);
  }

  ui->customWindowFrameCheckBox->blockSignals(true);
  ui->customWindowFrameCheckBox->setChecked(CustomTitleBar::isEnabled());
  ui->customWindowFrameCheckBox->blockSignals(false);

  ui->interfaceScaleSpinBox->blockSignals(true);
  ui->interfaceScaleSpinBox->setValue(Performance::interfaceScaleFactor());
  ui->interfaceScaleSpinBox->blockSignals(false);

  if (ui->proxyModeComboBox->count() == 0) {
    ui->proxyModeComboBox->blockSignals(true);
    ui->proxyModeComboBox->addItem(tr("System"), QStringLiteral("system"));
    ui->proxyModeComboBox->addItem(tr("None (direct)"), QStringLiteral("none"));
    ui->proxyModeComboBox->addItem(tr("SOCKS5"), QStringLiteral("socks5"));
    ui->proxyModeComboBox->addItem(tr("HTTP"), QStringLiteral("http"));
    ui->proxyModeComboBox->blockSignals(false);
  }
  ui->proxyModeComboBox->blockSignals(true);
  const int idx = ui->proxyModeComboBox->findData(NetworkProxy::mode());
  ui->proxyModeComboBox->setCurrentIndex(idx < 0 ? 0 : idx);
  ui->proxyModeComboBox->blockSignals(false);

  ui->proxyHostLineEdit->setText(NetworkProxy::host());
  ui->proxyPortSpinBox->blockSignals(true);
  ui->proxyPortSpinBox->setValue(NetworkProxy::port());
  ui->proxyPortSpinBox->blockSignals(false);
  ui->proxyUserLineEdit->setText(NetworkProxy::user());
  ui->proxyPasswordLineEdit->setText(NetworkProxy::password());

  // The host/port/credentials only make sense for a manual proxy.
  const QString m = NetworkProxy::mode();
  ui->proxyDetailsWidget->setEnabled(m == QLatin1String("socks5") ||
                                     m == QLatin1String("http"));

  // Notification-delivery backend (Linux only; hide the control elsewhere).
#ifdef Q_OS_LINUX
  if (ui->notificationBackendComboBox->count() == 0) {
    ui->notificationBackendComboBox->blockSignals(true);
    ui->notificationBackendComboBox->addItem(tr("Automatic"),
                                             QStringLiteral("auto"));
    ui->notificationBackendComboBox->addItem(tr("Desktop portal (Flatpak)"),
                                             QStringLiteral("portal"));
    ui->notificationBackendComboBox->addItem(tr("System service (libnotify)"),
                                             QStringLiteral("libnotify"));
    ui->notificationBackendComboBox->blockSignals(false);
  }
  {
    const QString backend = SettingsManager::instance()
                                .settings()
                                .value("notificationBackend", "auto")
                                .toString();
    ui->notificationBackendComboBox->blockSignals(true);
    const int bidx = ui->notificationBackendComboBox->findData(backend);
    ui->notificationBackendComboBox->setCurrentIndex(bidx < 0 ? 0 : bidx);
    ui->notificationBackendComboBox->blockSignals(false);
  }
#else
  ui->notificationBackendLabel->setVisible(false);
  ui->notificationBackendComboBox->setVisible(false);
#endif
}

void SettingsWidget::on_notificationBackendComboBox_currentIndexChanged(
    int index) {
  SettingsManager::instance().settings().setValue(
      "notificationBackend",
      ui->notificationBackendComboBox->itemData(index).toString());
}

void SettingsWidget::refreshJsAddonsList() {
  ui->jsAddonsList->blockSignals(true);
  ui->jsAddonsList->clear();
  const auto addons = CustomJs::addons();
  for (const CustomJs::Addon &a : addons) {
    auto *item = new QListWidgetItem(a.name, ui->jsAddonsList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(a.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, a.name);
  }
  ui->jsAddonsList->blockSignals(false);
  ui->removeJsAddonButton->setEnabled(ui->jsAddonsList->count() > 0);
}

void SettingsWidget::on_addJsAddonButton_clicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose a JavaScript file"), QString(),
      tr("JavaScript (*.js);;All files (*)"));
  if (path.isEmpty())
    return;
  QString error;
  if (CustomJs::addFromFile(path, &error).isEmpty()) {
    QMessageBox::warning(this, tr("Could not add addon"), error);
    return;
  }
  refreshJsAddonsList();
  emit customJsChanged();
}

void SettingsWidget::on_removeJsAddonButton_clicked() {
  auto *item = ui->jsAddonsList->currentItem();
  if (!item)
    return;
  const QString name = item->data(Qt::UserRole).toString();
  if (QMessageBox::question(
          this, tr("Remove addon"),
          tr("Remove the addon \"%1\"? This deletes its file.").arg(name)) !=
      QMessageBox::Yes)
    return;
  CustomJs::remove(name);
  refreshJsAddonsList();
  emit customJsChanged();
}

void SettingsWidget::on_jsAddonsList_itemChanged(QListWidgetItem *item) {
  if (!item)
    return;
  CustomJs::setEnabled(item->data(Qt::UserRole).toString(),
                       item->checkState() == Qt::Checked);
  emit customJsChanged();
}

void SettingsWidget::on_autostartCheckBox_toggled(bool checked) {
  if (!Autostart::setEnabled(checked)) {
    // Roll the checkbox back if the entry could not be written/removed.
    ui->autostartCheckBox->blockSignals(true);
    ui->autostartCheckBox->setChecked(!checked);
    ui->autostartCheckBox->blockSignals(false);
  }
}

void SettingsWidget::on_interfaceScaleSpinBox_valueChanged(double arg1) {
  Performance::setInterfaceScaleFactor(arg1);
}

void SettingsWidget::on_customWindowFrameCheckBox_toggled(bool checked) {
  CustomTitleBar::setEnabled(checked);
}

void SettingsWidget::on_proxyModeComboBox_currentIndexChanged(int index) {
  const QString m = ui->proxyModeComboBox->itemData(index).toString();
  NetworkProxy::setMode(m);
  ui->proxyDetailsWidget->setEnabled(m == QLatin1String("socks5") ||
                                     m == QLatin1String("http"));
  NetworkProxy::applyToApplication();
}

void SettingsWidget::on_proxyHostLineEdit_editingFinished() {
  NetworkProxy::setHost(ui->proxyHostLineEdit->text().trimmed());
  NetworkProxy::applyToApplication();
}

void SettingsWidget::on_proxyPortSpinBox_valueChanged(int arg1) {
  NetworkProxy::setPort(arg1);
  NetworkProxy::applyToApplication();
}

void SettingsWidget::on_proxyUserLineEdit_editingFinished() {
  NetworkProxy::setUser(ui->proxyUserLineEdit->text());
  NetworkProxy::applyToApplication();
}

void SettingsWidget::on_proxyPasswordLineEdit_editingFinished() {
  NetworkProxy::setPassword(ui->proxyPasswordLineEdit->text());
  NetworkProxy::applyToApplication();
}

void SettingsWidget::on_smoothScrollingCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("smoothScrolling", checked);
  // Live: applyUserSettings re-applies the QWebEngineSettings attribute to
  // every account profile.
  WebEngineProfileManager::instance().applyUserSettings();
}

// The themes are data, not UI: adding one to ChatTheme::themes() puts it in
// this list with no change here.
void SettingsWidget::populateChatThemes() {
  ui->chatThemeComboBox->blockSignals(true);
  ui->chatThemeComboBox->clear();
  const QString current = ChatTheme::currentThemeId();
  for (const ChatTheme::Theme &theme : ChatTheme::themes()) {
    ui->chatThemeComboBox->addItem(theme.name, theme.id);
    if (theme.id == current)
      ui->chatThemeComboBox->setCurrentIndex(ui->chatThemeComboBox->count() - 1);
  }
  ui->chatThemeComboBox->blockSignals(false);
}

// The dictionaries are whatever .bdic files the build installed, so a new
// language needs no code change here.
void SettingsWidget::populateSpellCheck() {
  const QStringList available = Dictionaries::availableDictionaries();

  ui->spellCheckCheckBox->blockSignals(true);
  ui->spellCheckCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("spellCheckEnabled", true)
          .toBool() &&
      !available.isEmpty());
  // Nothing to spell-check with is worth saying plainly, rather than offering a
  // switch that cannot do anything.
  ui->spellCheckCheckBox->setEnabled(!available.isEmpty());
  ui->spellCheckCheckBox->setText(
      available.isEmpty()
          ? tr("Spell checker (no dictionaries installed)")
          : tr("Check spelling as I type"));
  ui->spellCheckCheckBox->blockSignals(false);

  // The language picker is multi-select: Chromium can check against several
  // languages at once. A plain QComboBox is single-select, so its items are made
  // checkable and an event filter (installed once, below) toggles them on click
  // without closing the popup.
  const QStringList selected = Dictionaries::selectedDictionaries();
  auto *combo = ui->spellCheckLanguageComboBox;
  combo->blockSignals(true);
  if (!combo->isEditable()) {
    combo->setEditable(true);
    combo->lineEdit()->setReadOnly(true);
    combo->lineEdit()->setFocusPolicy(Qt::NoFocus);
  }
  auto *model = new QStandardItemModel(combo);
  for (const QString &dictionary : available) {
    // "es_ES" reads better as "Español (España)".
    const QLocale locale(dictionary);
    const QString name = locale.language() == QLocale::C
                             ? dictionary
                             : locale.nativeLanguageName() +
                                   QStringLiteral(" (") + dictionary +
                                   QStringLiteral(")");
    auto *item = new QStandardItem(name);
    item->setData(dictionary, Qt::UserRole);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(selected.contains(dictionary) ? Qt::Checked
                                                       : Qt::Unchecked);
    model->appendRow(item);
  }
  combo->setModel(model);
  connect(model, &QStandardItemModel::itemChanged, this,
          [this](QStandardItem *) { saveSpellCheckLanguages(); },
          Qt::UniqueConnection);
  // The view's items are toggled by a click that must not dismiss the popup.
  if (combo->view() && !combo->view()->viewport()->property("whatlyFilter").toBool()) {
    combo->view()->viewport()->installEventFilter(this);
    combo->view()->viewport()->setProperty("whatlyFilter", true);
  }
  combo->setEnabled(!available.isEmpty() && ui->spellCheckCheckBox->isChecked());
  combo->blockSignals(false);
  updateSpellCheckSummary();
}

// The combo shows a summary of what is checked rather than one entry's text.
void SettingsWidget::updateSpellCheckSummary() {
  const QStringList selected = Dictionaries::selectedDictionaries();
  QString text;
  if (selected.isEmpty())
    text = tr("None");
  else if (selected.size() == 1)
    text = selected.first();
  else
    text = tr("%1 languages").arg(selected.size());
  ui->spellCheckLanguageComboBox->setCurrentText(text);
  // A non-editable combo ignores setCurrentText, so also set it as the
  // placeholder-style display via the line edit if one exists.
  if (ui->spellCheckLanguageComboBox->lineEdit())
    ui->spellCheckLanguageComboBox->lineEdit()->setText(text);
}

void SettingsWidget::saveSpellCheckLanguages() {
  QStringList chosen;
  auto *model =
      qobject_cast<QStandardItemModel *>(ui->spellCheckLanguageComboBox->model());
  if (model)
    for (int i = 0; i < model->rowCount(); ++i)
      if (model->item(i)->checkState() == Qt::Checked)
        chosen << model->item(i)->data(Qt::UserRole).toString();
  SettingsManager::instance().settings().setValue("spellCheckLanguages", chosen);
  updateSpellCheckSummary();
  emit spellCheckChanged();
}

void SettingsWidget::on_themeToggleButtonCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/themeToggleButton", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_privacyBlurButtonCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/privacyBlurButton", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_spellCheckCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("spellCheckEnabled", checked);
  ui->spellCheckLanguageComboBox->setEnabled(checked);
  emit spellCheckChanged();
}



void SettingsWidget::populatePrivacyBlur() {
  ui->privacyBlurComboBox->blockSignals(true);
  ui->privacyBlurComboBox->clear();
  const QString current = PrivacyBlur::currentLevelId();
  for (const PrivacyBlur::Level &level : PrivacyBlur::levels()) {
    ui->privacyBlurComboBox->addItem(level.name, level.id);
    if (level.id == current)
      ui->privacyBlurComboBox->setCurrentIndex(
          ui->privacyBlurComboBox->count() - 1);
  }
  ui->privacyBlurComboBox->blockSignals(false);
}

void SettingsWidget::on_privacyBlurComboBox_currentIndexChanged(int index) {
  PrivacyBlur::setCurrentLevelId(
      ui->privacyBlurComboBox->itemData(index).toString());
  emit privacyBlurChanged();
}

void SettingsWidget::populateFontFamilies() {
  ui->fontFamilyComboBox->blockSignals(true);
  ui->fontFamilyComboBox->clear();
  const QString current = WebFont::currentFamily();
  // The empty id is WhatsApp's own font; every other entry is a system family
  // whose display text is the family name itself.
  ui->fontFamilyComboBox->addItem(tr("WhatsApp default"), QString());
  for (const QString &family : WebFont::families()) {
    if (family.isEmpty())
      continue;
    ui->fontFamilyComboBox->addItem(family, family);
    if (family == current)
      ui->fontFamilyComboBox->setCurrentIndex(ui->fontFamilyComboBox->count() -
                                              1);
  }
  ui->fontFamilyComboBox->blockSignals(false);
}

void SettingsWidget::on_fontFamilyComboBox_currentIndexChanged(int index) {
  WebFont::setCurrentFamily(ui->fontFamilyComboBox->itemData(index).toString());
  emit fontChanged();
}

void SettingsWidget::on_chatThemeComboBox_currentIndexChanged(int index) {
  ChatTheme::setCurrentThemeId(ui->chatThemeComboBox->itemData(index).toString());
  emit chatThemeChanged();
}

void SettingsWidget::updateChatWallpaperButtons() {
  ui->clearChatWallpaperButton->setEnabled(
      !ChatWallpaper::storedImagePath().isEmpty());
}

// The translations are compiled into the binary as :/i18n/<locale>.qm, so the
// picker is built by listing them: dropping a new .ts into src/i18n and adding
// it to CMakeLists is all it takes for a language to show up here.
void SettingsWidget::populateLanguages() {
  const QString current = SettingsManager::instance()
                              .settings()
                              .value("language")
                              .toString();

  ui->languageComboBox->blockSignals(true);
  ui->languageComboBox->clear();
  // An empty value means "follow the system", which stays the default.
  ui->languageComboBox->addItem(tr("System default"), QString());

  const QFileInfoList files =
      QDir(QStringLiteral(":/i18n")).entryInfoList({QStringLiteral("*.qm")},
                                                   QDir::Files, QDir::Name);
  for (const QFileInfo &file : files) {
    const QString code = file.completeBaseName(); // e.g. es_ES
    const QLocale locale(code);
    // Name the language in itself — an Italian speaker looks for "Italiano",
    // not for whatever the current interface language calls it.
    QString label = locale.nativeLanguageName();
    if (label.isEmpty())
      label = code;
    else
      label[0] = label[0].toUpper();
    ui->languageComboBox->addItem(QStringLiteral("%1 (%2)").arg(label, code),
                                  code);
  }

  const int index = ui->languageComboBox->findData(current);
  ui->languageComboBox->setCurrentIndex(index >= 0 ? index : 0);
  ui->languageComboBox->blockSignals(false);
}

void SettingsWidget::on_languageComboBox_currentIndexChanged(int index) {
  const QString code = ui->languageComboBox->itemData(index).toString();
  QSettings &settings = SettingsManager::instance().settings();
  if (settings.value("language").toString() == code)
    return;

  settings.setValue("language", code);
  // Qt would need every widget to be rebuilt to retranslate in place, so ask
  // for a restart rather than leave half the interface in the old language.
  emit notify(tr("The interface language will change when you restart %1.")
                  .arg(QApplication::applicationDisplayName()));
}

void SettingsWidget::on_identifyInLinkedDevicesCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("identifyInLinkedDevices",
                                                  checked);
  emit linkedDeviceNameChanged();
}

void SettingsWidget::on_appAutoLockcheckBox_toggled(bool checked) {
  if (SettingsManager::instance().settings().value("asdfg").isValid()) {
    SettingsManager::instance().settings().setValue("appAutoLocking", checked);
  } else {
    QMessageBox::information(this, tr("App Lock Setup"),
                             tr("Please setup the App lock password first."),
                             QMessageBox::Ok);
    if (SettingsManager::instance().settings().value("asdfg").isValid() ==
        false) {
      SettingsManager::instance().settings().setValue("appAutoLocking", false);
      autoAppLockSetChecked(false);
    }
  }
  emit appAutoLockChanged();
}

void SettingsWidget::on_autoLockDurationSpinbox_valueChanged(int arg1) {
  SettingsManager::instance().settings().setValue("autoLockDuration", arg1);
  emit appAutoLockChanged();
}

void SettingsWidget::on_resetAppAutoLockPushButton_clicked() {
  ui->appAutoLockcheckBox->setChecked(defaultAppAutoLock);
  ui->autoLockDurationSpinbox->setValue(defaultAppAutoLockDuration);
}

void SettingsWidget::on_minimizeOnTrayIconClick_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("minimizeOnTrayIconClick",
                                                  checked);
}

void SettingsWidget::on_styleComboBox_currentTextChanged(const QString &arg1) {
  applyThemeQuirks();
  SettingsManager::instance().settings().setValue("widgetStyle", arg1);
  emit updateWindowTheme();
  emit updatePageTheme();
}

void SettingsWidget::on_zoomPlus_clicked() {
  double currentFactor = SettingsManager::instance()
                             .settings()
                             .value("zoomFactor", 1.0)
                             .toDouble();
  double newFactor = currentFactor + 0.25;
  ui->zoomFactorSpinBox->setValue(newFactor);
  SettingsManager::instance().settings().setValue(
      "zoomFactor", ui->zoomFactorSpinBox->value());
  emit zoomChanged();
}

void SettingsWidget::on_zoomMinus_clicked() {
  double currentFactor = SettingsManager::instance()
                             .settings()
                             .value("zoomFactor", 1.0)
                             .toDouble();
  double newFactor = currentFactor - 0.25;
  ui->zoomFactorSpinBox->setValue(newFactor);
  SettingsManager::instance().settings().setValue(
      "zoomFactor", ui->zoomFactorSpinBox->value());
  emit zoomChanged();
}

void SettingsWidget::on_zoomReset_clicked() {
  ui->zoomFactorSpinBox->setValue(1.0);
  SettingsManager::instance().settings().setValue(
      "zoomFactor", ui->zoomFactorSpinBox->value());
  emit zoomChanged();
}

void SettingsWidget::on_zoomResetMaximized_clicked() {
  ui->zoomFactorSpinBoxMaximized->setValue(defaultZoomFactorMaximized);
  SettingsManager::instance().settings().setValue(
      "zoomFactorMaximized", ui->zoomFactorSpinBoxMaximized->value());
  emit zoomMaximizedChanged();
}

void SettingsWidget::on_zoomPlusMaximized_clicked() {
  double currentFactor =
      SettingsManager::instance()
          .settings()
          .value("zoomFactorMaximized", defaultZoomFactorMaximized)
          .toDouble();
  double newFactor = currentFactor + 0.25;
  ui->zoomFactorSpinBoxMaximized->setValue(newFactor);
  SettingsManager::instance().settings().setValue(
      "zoomFactorMaximized", ui->zoomFactorSpinBoxMaximized->value());
  emit zoomMaximizedChanged();
}

void SettingsWidget::on_zoomMinusMaximized_clicked() {
  double currentFactor =
      SettingsManager::instance()
          .settings()
          .value("zoomFactorMaximized", defaultZoomFactorMaximized)
          .toDouble();
  double newFactor = currentFactor - 0.25;
  ui->zoomFactorSpinBoxMaximized->setValue(newFactor);
  SettingsManager::instance().settings().setValue(
      "zoomFactorMaximized", ui->zoomFactorSpinBoxMaximized->value());
  emit zoomMaximizedChanged();
}

void SettingsWidget::on_changeDefaultDownloadLocationPb_clicked() {
  QFileDialog dialog(this);
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly);

  QString path;
  bool usenativeFileDialog = SettingsManager::instance()
                                 .settings()
                                 .value("useNativeFileDialog", true)
                                 .toBool();
  if (usenativeFileDialog == false) {
    path = QFileDialog::getExistingDirectory(
        this, tr("Select download directory"),
        SettingsManager::instance()
            .settings()
            .value("defaultDownloadLocation",
                   QStandardPaths::writableLocation(
                       QStandardPaths::DownloadLocation) +
                       QDir::separator() + QApplication::applicationDisplayName())
            .toString(),
        QFileDialog::DontUseNativeDialog);
  } else {
    path = QFileDialog::getSaveFileName(
        this, tr("Select download directory"),
        SettingsManager::instance()
            .settings()
            .value("defaultDownloadLocation",
                   QStandardPaths::writableLocation(
                       QStandardPaths::DownloadLocation) +
                       QDir::separator() + QApplication::applicationDisplayName())
            .toString());
  }

  if (!path.isNull() && !path.isEmpty()) {
    ui->defaultDownloadLocation->setText(path);
    SettingsManager::instance().settings().setValue("defaultDownloadLocation",
                                                    path);
  }
}

void SettingsWidget::on_userAgentLineEdit_editingFinished() {
  ui->userAgentLineEdit->home(true);
  ui->userAgentLineEdit->deselect();
}

void SettingsWidget::on_viewPassword_clicked() {
  ui->current_password->setEchoMode(QLineEdit::Normal);
  ui->viewPassword->setEnabled(false);
  ui->current_password->setFocus();
  QTimer *timer = new QTimer(this);
  timer->setInterval(5000);
  connect(timer, &QTimer::timeout, ui->current_password, [=]() {
    ui->current_password->setEchoMode(QLineEdit::Password);
    ui->viewPassword->setEnabled(true);
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

void SettingsWidget::on_chnageCurrentPasswordPushButton_clicked() {
  if (SettingsManager::instance().settings().value("asdfg").isValid()) {
    QMessageBox msgBox;
    msgBox.setText(tr("You are about to change your current app lock password!"
                   "\n\nThis will LogOut your current session."
                   "\nYou may also require a complete restart of Application!"));
    msgBox.setIconPixmap(
        QPixmap(":/icons/information-line.png")
            .scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    msgBox.setInformativeText(tr("Do you want to proceed?"));
    msgBox.setStandardButtons(QMessageBox::Cancel);
    QPushButton *changePassword =
        new QPushButton(this->style()->standardIcon(QStyle::SP_DialogYesButton),
                        "Change password", nullptr);
    msgBox.addButton(changePassword, QMessageBox::NoRole);
    connect(changePassword, &QPushButton::clicked, changePassword, [=]() {
      this->close();
      emit changeLockPassword();
    });
    msgBox.exec();

  } else {
    SettingsManager::instance().settings().setValue("lockscreen", true);
    showSetApplockPasswordDialog();
  }
}

void SettingsWidget::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape)
    this->close();

  QWidget::keyPressEvent(e);
}
