#include "settingswidget.h"
#include "ui_settingswidget.h"

#include "mainwindow.h"
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QImageReader>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>
#include <QLocale>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollBar>
#include <QTimer>
#include <QPropertyAnimation>

#include "automatictheme.h"
#include "chattheme.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "webengineprofilemanager.h"
#include "dictionaries.h"
#include "dictionaryrows.h"
#include "chatliststrip.h"
#include "privacyblur.h"
#include "webfont.h"
#include "mutedstatus.h"
#include "performance.h"
#include "cloudapi.h"
#include "localapi.h"
#include "cloudwebhook.h"
#include "networkproxy.h"
#include "autostart.h"
#include "customjs.h"
#include "customtitlebar.h"
#include "notificationrules.h"
#include "updatechecker.h"
#include "storageinfo.h"
#include "shortcuts.h"
#include "backup.h"
#include "screenlock.h"
#include "focusmode.h"
#include "hdmedia.h"
#include "undosend.h"
#include "translator.h"
#include "aiassistant.h"
#include "ollama.h"
#include "cannedresponses.h"

#include <QListWidget>
#include <QInputDialog>
#include <QTimeEdit>
#include <QTime>
#include <QFormLayout>
#include <QKeySequenceEdit>

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

  // Which build this is, under the window's own title, where someone who wants
  // it knows to look and nobody else has to see it. This is the one place it is
  // written out: a window with messages in it says nothing about builds, and
  // almost nobody reading messages has ever wanted to know.
  ui->buildLabel->setText(Utils::versionLabel());
  Utils::makeWatermark(ui->buildLabel);

#ifdef Q_OS_WIN
  // The Qt "widget style" chooser is a Linux desktop-theming nicety; on Windows
  // the native style is expected, so the control is hidden there.
  ui->label_8->hide();
  ui->styleComboBox->hide();
#endif

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
  ui->notificationSoundCheckBox->setChecked(SettingsManager::instance()
                                                .settings()
                                                .value("notificationSound", true)
                                                .toBool());
  ui->useNativeFileDialog->setChecked(SettingsManager::instance()
                                          .settings()
                                          .value("useNativeFileDialog", true)
                                          .toBool());
  // Loading must not run the tray mutual-exclusion in the toggled slots, which
  // would rewrite the saved values in load order rather than by user intent.
  ui->startMinimized->blockSignals(true);
  ui->startMinimized->setChecked(SettingsManager::instance()
                                     .settings()
                                     .value("startMinimized", false)
                                     .toBool());
  ui->startMinimized->blockSignals(false);
  ui->rememberWindowLayoutCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("rememberWindowLayout", false)
          .toBool());
  ui->unreadCountMutedCheckBox->setChecked(MainWindow::unreadCountIncludesMuted());
  ui->unreadCountArchivedCheckBox->setChecked(
      MainWindow::unreadCountIncludesArchived());
  ui->unreadCountMessagesCheckBox->setChecked(
      MainWindow::unreadCountCountsMessages());
  // Each one changes what a number on screen means, so the number is worked out
  // again at once rather than at the next message to arrive anywhere.
  const auto recount = [this]() {
    if (auto *w = qobject_cast<MainWindow *>(this->parent()))
      w->countUnreadEverywhere();
  };
  connect(ui->unreadCountMutedCheckBox, &QCheckBox::toggled, this,
          [recount](bool on) {
            MainWindow::setUnreadCountIncludesMuted(on);
            recount();
          });
  connect(ui->unreadCountArchivedCheckBox, &QCheckBox::toggled, this,
          [recount](bool on) {
            MainWindow::setUnreadCountIncludesArchived(on);
            recount();
          });
  connect(ui->unreadCountMessagesCheckBox, &QCheckBox::toggled, this,
          [recount](bool on) {
            MainWindow::setUnreadCountCountsMessages(on);
            recount();
          });
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
  ui->zoomButtonsCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("webtweaks/zoomButtons", true)
          .toBool());
  ui->chatListStripButtonCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("webtweaks/chatListStripButton", true)
          .toBool());
  ui->identifyInLinkedDevicesCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("identifyInLinkedDevices", true)
          .toBool());
  populateLanguages();
  populateChatThemes();
  populatePrivacyBlur();
  populateChatListPreviewSize();
  populateFontFamilies();
  populateSpellCheck();
  updateCustomCssButtons();
  ui->smoothScrollingCheckBox->setChecked(
      SettingsManager::instance().settings().value("smoothScrolling", false).toBool());
  ui->monochromeTrayIconCheckBox->setChecked(
      SettingsManager::instance().settings().value("monochromeTrayIcon", false).toBool());
  ui->hideTrayIconCheckBox->blockSignals(true);
  ui->hideTrayIconCheckBox->setChecked(
      SettingsManager::instance().settings().value("hideTrayIcon", false).toBool());
  ui->hideTrayIconCheckBox->blockSignals(false);
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
  loadNotificationRules();
  loadShortcuts();
  loadCloudApiSettings();
  loadLocalApiSettings();
  refreshJsAddonsList();
  refreshCannedList();
  ui->lockOnMinimizeCheckBox->setChecked(
      SettingsManager::instance().settings().value("lockOnHideToTray", false).toBool());
  ui->lockOnScreenLockCheckBox->blockSignals(true);
  ui->lockOnScreenLockCheckBox->setChecked(ScreenLock::isEnabled());
  ui->lockOnScreenLockCheckBox->blockSignals(false);
#ifndef Q_OS_LINUX
  ui->lockOnScreenLockCheckBox->setVisible(false);
#endif
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
  ui->minimizeOnTrayIconClick->blockSignals(true);
  ui->minimizeOnTrayIconClick->setChecked(
      SettingsManager::instance()
          .settings()
          .value("minimizeOnTrayIconClick", false)
          .toBool());
  ui->minimizeOnTrayIconClick->blockSignals(false);
  ui->minimizeOnlyFocusedWindowCheckBox->blockSignals(true);
  ui->minimizeOnlyFocusedWindowCheckBox->setChecked(
      SettingsManager::instance()
          .settings()
          .value("minimizeOnlyFocusedWindow", false)
          .toBool());
  ui->minimizeOnlyFocusedWindowCheckBox->blockSignals(false);
  ui->defaultDownloadLocation->setText(QDir::toNativeSeparators(
      SettingsManager::instance()
          .settings()
          .value("defaultDownloadLocation",
                 QStandardPaths::writableLocation(
                     QStandardPaths::DownloadLocation) +
                     QLatin1Char('/') + QApplication::applicationDisplayName())
          .toString()));

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

  // The passcode is stored hashed (issue #42) and must never be shown; the
  // viewer only reflects whether one is set.
  this->setCurrentPasswordText(
      SettingsManager::instance().settings().value("asdfg").isValid()
          ? QString()
          : QStringLiteral("Require setup"));

  applyThemeQuirks();

  ui->setUserAgent->setEnabled(false);

  // event filter to prevent wheel event on certain widgets
  foreach (QSlider *slider, this->findChildren<QSlider *>()) {
    slider->installEventFilter(this);
  }
  foreach (QComboBox *box, this->findChildren<QComboBox *>()) {
    box->installEventFilter(this);
  }
  // QAbstractSpinBox, not QSpinBox: the interface scale and the two zoom factors
  // are QDoubleSpinBox, which is a sibling of QSpinBox and not a subclass of it —
  // so those three were left out of this guard and a wheel scrolling the page went
  // on changing them under the pointer. The interface scale is the one that hurts:
  // it only takes effect at the next launch, so the app comes back at half size
  // with nothing to connect it to.
  foreach (QAbstractSpinBox *spinBox, this->findChildren<QAbstractSpinBox *>()) {
    spinBox->installEventFilter(this);
  }
  // The lists inside the page scroll themselves and keep the wheel that starts
  // on them, but a page scroll passing over one must not stop dead there — which
  // is what happens when Qt hands them the event directly.
  foreach (QAbstractScrollArea *area,
           this->findChildren<QAbstractScrollArea *>()) {
    if (area != ui->scrollArea)
      area->viewport()->installEventFilter(this);
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

  // ── Re-section "General settings" + collapsible accordion ───────────────────
  // The single "General settings" group had grown overwhelming, so its controls
  // are redistributed into themed sub-sections. Every sub-section — and each
  // pre-existing group below it — becomes a collapsible row with an arrow header
  // (▸ collapsed / ▾ open); toggling hides/shows the WHOLE group as one widget,
  // so a collapsed section shrinks to just its header instead of an empty box.
  // Only the first row is open on launch.
  if (auto *outer =
          qobject_cast<QVBoxLayout *>(ui->scrollAreaWidgetContents->layout())) {
    QWidget *host = ui->scrollAreaWidgetContents;

    // An empty titled sub-section, ready to receive controls.
    int sectionCount = 0;
    const auto newSection = [host, &sectionCount](const QString &title) {
      auto *g = new QGroupBox(title, host);
      // Named so makeCollapsible can draw its outline by object name; a bare
      // "QGroupBox" rule would reach the nested groups inside it too. The name
      // must be a plain ASCII identifier: it goes into a Qt style-sheet selector
      // (QGroupBox#name), whose parser rejects the accents and slashes a
      // translated title can carry ("Básico", "IA/traducción") — which is why it
      // is an index, not the title.
      g->setObjectName(
          QStringLiteral("whatlySection%1").arg(sectionCount++));
      auto *v = new QVBoxLayout(g);
      v->setContentsMargins(12, 6, 6, 6);
      v->setSpacing(6);
      return g;
    };
    const auto body = [](QGroupBox *g) {
      return qobject_cast<QVBoxLayout *>(g->layout());
    };
    // Move a whole nested layout, detaching it from its current parent layout.
    const auto moveLayout = [](QVBoxLayout *dst, QLayout *inner) {
      if (!dst || !inner)
        return;
      if (auto *p = qobject_cast<QLayout *>(inner->parent()))
        p->removeItem(inner);
      dst->addLayout(inner);
    };
    // Move one control (detached from its source layout) onto its own row. An
    // `end` widget rides at the right-hand end of that row instead of costing a
    // row of its own — the shape the zoom controls already use.
    const auto moveWidget = [](QVBoxLayout *dst, QWidget *w, QLayout *src,
                               QWidget *end = nullptr) {
      if (!dst || !w)
        return;
      if (src)
        src->removeWidget(w);
      if (!end) {
        dst->addWidget(w);
        return;
      }
      auto *h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(w);
      h->addStretch(1);
      h->addWidget(end);
      dst->addLayout(h);
    };
    // Move a label + field pair onto a single row.
    const auto moveRow = [](QVBoxLayout *dst, QWidget *a, QWidget *b,
                            QLayout *src, QWidget *end = nullptr) {
      if (!dst)
        return;
      auto *h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      if (a) {
        if (src)
          src->removeWidget(a);
        h->addWidget(a);
      }
      if (b) {
        if (src)
          src->removeWidget(b);
        h->addWidget(b, 1);
      }
      if (end)
        h->addWidget(end);
      dst->addLayout(h);
    };
    // A second "Restart now", for a section that has one setting needing a
    // restart rather than a run of them. Same words and same tooltip as the
    // button in the .ui, so there is nothing extra to translate.
    const auto restartButton = [this]() {
      auto *b = new QPushButton(ui->restartNowButton->text(), this);
      b->setToolTip(ui->restartNowButton->toolTip());
      b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
      connect(b, &QPushButton::clicked, this,
              &SettingsWidget::restartRequested);
      return b;
    };
    // Move a label + a nested control-layout pair onto a single row.
    const auto moveRowL = [](QVBoxLayout *dst, QWidget *a, QLayout *sub,
                             QLayout *srcOfA) {
      if (!dst)
        return;
      auto *h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      if (a) {
        if (srcOfA)
          srcOfA->removeWidget(a);
        h->addWidget(a);
      }
      if (sub) {
        if (auto *p = qobject_cast<QLayout *>(sub->parent()))
          p->removeItem(sub);
        h->addLayout(sub, 1);
      }
      dst->addLayout(h);
    };

    QLayout *G = ui->gridLayout;    // the old loose grab-bag grid
    QLayout *G6 = ui->gridLayout_6; // close-button / shortcuts / permissions

    // ── Basics ──────────────────────────────────────────────
    auto *basics = newSection(tr("Basics"));
    moveRow(body(basics), ui->languageLabel, ui->languageComboBox, G,
            restartButton());
    moveLayout(body(basics), ui->gridLayout_7); // default download location
    moveWidget(body(basics), ui->useNativeFileDialog, G);
    moveWidget(body(basics), ui->identifyInLinkedDevicesCheckBox, G);

    // ── Appearance ──────────────────────────────────────────
    auto *appearance = newSection(tr("Appearance"));
    moveLayout(body(appearance), ui->gridLayout_5);     // display theme block
    moveLayout(body(appearance), ui->horizontalLayout); // widget style (Linux)
    moveRow(body(appearance), ui->chatThemeLabel, ui->chatThemeComboBox, G);
    moveRowL(body(appearance), ui->chatWallpaperLabel, ui->chatWallpaperLayout,
             G);
    moveRow(body(appearance), ui->fontFamilyLabel, ui->fontFamilyComboBox, G);
    moveRow(body(appearance), ui->interfaceFontSizeLabel,
            ui->interfaceFontSizeSpinBox, G);
    moveRowL(body(appearance), ui->customCssLabel, ui->customCssLayout, G);
    moveWidget(body(appearance), ui->themeToggleButtonCheckBox, G);
    moveWidget(body(appearance), ui->zoomButtonsCheckBox, G);
    moveWidget(body(appearance), ui->chatListStripButtonCheckBox, G);
    moveRow(body(appearance), ui->chatListPreviewSizeLabel,
            ui->chatListPreviewSizeComboBox, G);
    moveWidget(body(appearance), ui->smoothScrollingCheckBox, G);
    moveWidget(body(appearance), ui->monochromeTrayIconCheckBox, G);

    // ── Notifications ───────────────────────────────────────
    auto *notifications = newSection(tr("Notifications"));
    moveLayout(body(notifications), ui->gridLayout_8); // whole notice block

    // ── Chatting ────────────────────────────────────────────
    auto *chatting = newSection(tr("Chatting"));
    moveRow(body(chatting), ui->spellCheckCheckBox,
            ui->spellCheckLanguageComboBox, G);
    moveWidget(body(chatting), ui->muteAudioCheckBox, G);
    moveWidget(body(chatting), ui->autoPlayMediaCheckBox, G);
    moveWidget(body(chatting), ui->dismissEmojiPanelCheckBox, G);
    moveWidget(body(chatting), ui->unreadCountMutedCheckBox, G);
    moveWidget(body(chatting), ui->unreadCountArchivedCheckBox, G);
    moveWidget(body(chatting), ui->unreadCountMessagesCheckBox, G);
    moveWidget(body(chatting), ui->hideMutedStatusCheckBox, G);
    // Two everyday messaging preferences that were filed under "Performance &
    // Privacy", where nobody would think to look for them: how photos are sent,
    // and whether Enter holds a message briefly before it goes. Neither has
    // anything to do with performance.
    moveWidget(body(chatting), ui->hdMediaCheckBox,
               ui->verticalLayoutPerformance);
    moveLayout(body(chatting), ui->horizontalLayoutUndoSend);

    // ── Spell-check dictionaries ─────────────────────────────
    // The full set is ~43 MB, so packages bundle a minimum and the rest are
    // fetched on demand (issue #46). The list that does it is the language picker
    // in "Chatting" above: every language gets a row there, and the row carries
    // the one control its state allows — a tick box and a bin for one you have, a
    // download arrow for one you have not.
    if (!m_dictManager)
      m_dictManager = new DictionaryManager(this);
    connect(m_dictManager, &DictionaryManager::catalogReady, this,
            [this](const QList<DictionaryEntry> &entries) {
              m_dictCatalog = entries;
              m_dictCatalogError.clear();
              m_dictCatalogTries = 0;
              populateSpellCheck(); // offer the downloadable languages in the picker

              // One-time: if nothing is chosen yet and the system locale's
              // dictionary is downloadable but not installed, fetch it so the
              // user's own language works out of the box (issue #46).
              auto &s = SettingsManager::instance().settings();
              if (s.value(QStringLiteral("dictLocaleFetched"), false).toBool())
                return;
              s.setValue(QStringLiteral("dictLocaleFetched"), true);
              if (!s.value(QStringLiteral("spellCheckLanguages"))
                       .toStringList()
                       .isEmpty())
                return;
              const QStringList have = Dictionaries::availableDictionaries();
              const QString loc = QLocale::system().name();
              const QString lang = loc.section(QLatin1Char('_'), 0, 0);
              for (const DictionaryEntry &e : m_dictCatalog) {
                if (have.contains(e.code))
                  continue;
                if (e.code == loc || e.code == lang ||
                    e.code.startsWith(lang + QLatin1Char('_'))) {
                  m_dictManager->download(e);
                  break;
                }
              }
            });

    // The list of downloadable languages comes from a GitHub release, and a fetch
    // of it fails from time to time for reasons that have nothing to do with the
    // user — an HTTP/2 stream refused on the way through the redirect is the one
    // seen in practice. Try again twice before believing it, and then say so on a
    // row instead of showing a list of the two installed languages as if that were
    // all there is.
    connect(m_dictManager, &DictionaryManager::catalogFailed, this,
            [this](const QString &error) {
              qWarning() << "whatly: could not fetch the dictionary catalogue:"
                         << error;
              m_dictCatalogError = error;
              if (m_dictCatalogTries < 3) {
                QTimer::singleShot(m_dictCatalogTries * 2000 - 1000, this,
                                   [this] { fetchDictionaryCatalog(); });
                return;
              }
              populateSpellCheck();
            });

    // A download in flight shows its per-cent on its own row, so the list is also
    // where you watch it arrive.
    connect(m_dictManager, &DictionaryManager::downloadProgress, this,
            [this](const QString &code, int percent) {
              setSpellCheckRowProgress(code, percent);
            });
    connect(m_dictManager, &DictionaryManager::downloadFinished, this,
            [this](const QString &code, bool ok, const QString &error) {
              if (!ok) {
                setSpellCheckRowProgress(code, DictionaryRows::Failed, error);
                return;
              }
              // A language you went and fetched is one you mean to write in, so it
              // arrives ticked; re-reading the list hands it to Chromium, which is
              // what makes it work without a restart.
              QStringList chosen = SettingsManager::instance()
                                       .settings()
                                       .value(QStringLiteral("spellCheckLanguages"))
                                       .toStringList();
              if (!chosen.contains(code)) {
                chosen << code;
                SettingsManager::instance().settings().setValue(
                    QStringLiteral("spellCheckLanguages"), chosen);
              }
              populateSpellCheck();
              emit spellCheckChanged();
            });
    fetchDictionaryCatalog(); // the downloadable languages, for the picker

    // ── Privacy & Lock ──────────────────────────────────────
    auto *privacy = newSection(tr("Privacy & Lock"));
    moveRow(body(privacy), ui->privacyBlurLabel, ui->privacyBlurComboBox, G);
    moveWidget(body(privacy), ui->privacyBlurButtonCheckBox, G);
    moveLayout(body(privacy), ui->gridLayout_3); // app-lock block
    // The two genuinely privacy settings out of the old "Performance & Privacy"
    // group — which is what the "& Privacy" in its name was promising. Focus mode
    // hides chat-list previews from anyone looking at the screen, and the WebRTC
    // shield stops calls leaking the local address.
    moveWidget(body(privacy), ui->focusModeCheckBox,
               ui->verticalLayoutPerformance);
    moveWidget(body(privacy), ui->webrtcShieldCheckBox,
               ui->verticalLayoutPerformance);

    // ── Window & zoom ───────────────────────────────────────
    auto *window = newSection(tr("Window && zoom"));
    moveRow(body(window), ui->label, ui->closeButtonActionComboBox, G6);
    moveWidget(body(window), ui->startMinimized, G);
    moveWidget(body(window), ui->minimizeOnTrayIconClick, G);
    moveWidget(body(window), ui->rememberWindowLayoutCheckBox, G);
    moveWidget(body(window), ui->hideTrayIconCheckBox, G);
    // These settings shape the window's own chrome, so they belong here rather
    // than under "Network & Startup", where the frame checkbox had ended up
    // next to the autostart one and was not where anyone looked for it. The
    // frame checkbox keeps the Restart-now button beside it (#27).
    moveWidget(body(window), ui->alwaysShowAccountTabsCheckBox, G);
    moveWidget(body(window), ui->customWindowFrameCheckBox, G,
               ui->restartNowButton);
    moveWidget(body(window), ui->tabsInTitleBarCheckBox, G);
    moveLayout(body(window), ui->gridLayout_9); // zoom block
    // Interface scale belongs beside the zoom controls, not under "Network &
    // Startup" where it sat next to the autostart checkbox — the same mistake the
    // frame checkbox made, and this section already carries the Restart-now
    // button its label asks for.
    moveLayout(body(window), ui->horizontalLayoutScale);
    // Last in the section: it qualifies how the windows this section configures
    // are put away, rather than being one more of the tray checkboxes above.
    moveWidget(body(window), ui->minimizeOnlyFocusedWindowCheckBox, G);

    // ── AI & translation ────────────────────────────────────
    // The largest thing in the old "Performance & Privacy" group by far: two
    // whole feature panels, each with an endpoint, a model or target language and
    // an API key. They are neither performance nor privacy, and burying them
    // there is most of why that group was impossible to navigate.
    auto *aiTranslation = newSection(tr("AI && translation"));
    moveWidget(body(aiTranslation), ui->aiGroupBox,
               ui->verticalLayoutPerformance);
    moveWidget(body(aiTranslation), ui->translationGroupBox,
               ui->verticalLayoutPerformance);

    // ── Advanced ────────────────────────────────────────────
    auto *advanced = newSection(tr("Advanced"));
    moveLayout(body(advanced), ui->horizontalLayout_3); // user agent
    moveWidget(body(advanced), ui->autoRestartCheckBox, G);
    moveRow(body(advanced), ui->label_9, ui->showShortcutsButton, G6);
    moveRow(body(advanced), ui->label_10, ui->showPermissionsButton, G6);

    // The emptied "General settings" shell (and its leftover separator lines)
    // are no longer needed; the arrow headers separate the sections now.
    outer->removeWidget(ui->groupBox_8);
    delete ui->groupBox_8;

    // Place the everyday sub-sections above the pre-existing groups, in reading
    // order; "Advanced" is pushed to the very bottom of the whole list.
    outer->insertWidget(0, basics);
    outer->insertWidget(1, appearance);
    outer->insertWidget(2, notifications);
    outer->insertWidget(3, chatting);
    outer->insertWidget(4, aiTranslation);
    outer->insertWidget(5, privacy);
    outer->insertWidget(6, window);
    outer->addWidget(advanced);

    QScrollArea *scrollArea = ui->scrollArea;
    const auto makeCollapsible = [outer, scrollArea](QGroupBox *box,
                                                     bool open) {
      if (!box)
        return;
      const int idx = outer->indexOf(box);
      if (idx < 0)
        return;
      delete outer->takeAt(idx); // detach the bare group from the column

      auto *section = new QWidget(box->parentWidget());
      auto *sv = new QVBoxLayout(section);
      sv->setContentsMargins(0, 0, 0, 0);
      sv->setSpacing(0);

      auto *header = new QToolButton(section);
      header->setText(box->title());
      header->setCheckable(true);
      header->setChecked(open);
      header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
      header->setAutoRaise(true);
      header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      header->setStyleSheet("QToolButton { border: none; font-weight: bold; "
                            "text-align: left; padding: 3px 6px; }");

      box->setTitle(QString()); // the header shows the title now
      // An outline around an open section's contents, so it reads as one block
      // belonging to the header above it rather than as loose controls. Scoped
      // by object name: a plain "QGroupBox" rule would also outline the groups
      // nested inside, doubling the borders.
      box->setStyleSheet(
          QStringLiteral("QGroupBox#%1{border:1px solid palette(mid);"
                         "border-radius:6px;margin-top:0px;padding:2px}")
              .arg(box->objectName()));
      box->setVisible(open);

      QObject::connect(
          header, &QToolButton::toggled, box,
          [box, header, section, scrollArea](bool on) {
            box->setVisible(on);
            header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
            if (!on || !scrollArea)
              return;
            // Newly revealed content may extend past the bottom of the window;
            // nudge the view down just enough to bring this section's last item
            // back on screen (never scrolls up).
            QTimer::singleShot(0, scrollArea, [scrollArea, section]() {
              QWidget *content = scrollArea->widget();
              if (!content)
                return;
              if (QLayout *l = content->layout())
                l->activate(); // settle geometry before measuring
              const int sectionBottom =
                  section->mapTo(content, QPoint(0, section->height())).y();
              QScrollBar *vsb = scrollArea->verticalScrollBar();
              const int need = sectionBottom - scrollArea->viewport()->height();
              if (need <= vsb->value())
                return;
              // Ease the scroll over ~1.5 s (reusing one animation per scrollbar
              // so rapid toggles don't fight) so the jump is easy to follow
              // rather than an instant snap.
              auto *anim = vsb->findChild<QPropertyAnimation *>();
              if (!anim) {
                anim = new QPropertyAnimation(vsb, "value", vsb);
                anim->setEasingCurve(QEasingCurve::InOutCubic);
              }
              anim->stop();
              anim->setDuration(1500);
              anim->setStartValue(vsb->value());
              anim->setEndValue(qMin(need, vsb->maximum()));
              anim->start();
            });
          });

      sv->addWidget(header);
      sv->addWidget(box); // reparents the group into the section
      outer->insertWidget(idx, section);
    };

    makeCollapsible(basics, true); // open on launch
    makeCollapsible(appearance, false);
    makeCollapsible(notifications, false);
    makeCollapsible(chatting, false);
    makeCollapsible(aiTranslation, false);
    makeCollapsible(privacy, false);
    makeCollapsible(window, false);
    makeCollapsible(advanced, false);
    // Give each group made up entirely of restart-only settings its own
    // "Restart now" button (bottom-right) — the same one offered beside
    // Interface language and the window-frame controls (#27). One per group.
    const auto addGroupRestart = [restartButton](QVBoxLayout *v) {
      if (!v)
        return;
      auto *h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      h->addStretch(1);
      h->addWidget(restartButton());
      v->addLayout(h);
    };
    addGroupRestart(ui->verticalLayoutPerformance);
    addGroupRestart(ui->verticalLayoutJsAddons);
    addGroupRestart(ui->verticalLayoutShortcuts);

    makeCollapsible(ui->groupBox_7, false);          // Storage
    makeCollapsible(ui->groupBoxPerformance, false); // Performance
    makeCollapsible(ui->groupBoxNetwork, false);     // Network & Startup
    makeCollapsible(ui->groupBoxJsAddons, false);    // JS Addons
    makeCollapsible(ui->groupBoxCanned, false);      // Canned responses
    makeCollapsible(ui->groupBoxShortcuts, false);   // Shortcuts
    // These two arrived after the redesign and were left as bare groups, so
    // they were the only settings on the page with no header to fold them away.
    //
    // PLEASE KEEP IT THAT WAY: every setting on this page lives inside a
    // section that folds. A new one goes into whichever section above it
    // belongs to; a new group of them gets a header of its own and joins this
    // list. Nothing should sit loose on the page — that is how the custom
    // window frame ended up somewhere nobody looked for it.
    makeCollapsible(ui->groupBoxCloudApi, false);    // Cloud API
    makeCollapsible(ui->groupBoxLocalApi, false);    // Local API & webhooks

    // Stack the section headers directly beneath one another with about one
    // character of breathing room, instead of letting the column stretch them
    // apart to fill the window height.
    outer->setSpacing(this->fontMetrics().averageCharWidth());
    outer->addStretch(1);
  }
}

bool SettingsWidget::eventFilter(QObject *obj, QEvent *event) {

  // The spell-check combo has to be editable to show a summary of what is ticked
  // rather than one entry's text — and an editable combo opens its list only when
  // the arrow is clicked, so clicking the box itself did nothing whatsoever. Open
  // it from anywhere on the box, the way every other combo on this page behaves.
  if (event->type() == QEvent::MouseButtonPress &&
      ui->spellCheckLanguageComboBox->lineEdit() &&
      obj == ui->spellCheckLanguageComboBox->lineEdit()) {
    auto *combo = ui->spellCheckLanguageComboBox;
    // Hand the click to the arrow rather than calling showPopup() ourselves.
    //
    // showPopup() alone made the list flash and vanish: it appears under the
    // pointer and the release that follows lands on it, which a popup shown this
    // instant reads as a click outside itself. Qt's own arrow path is the only one
    // that swallows that release — QComboBox::mousePressEvent starts the container's
    // block-release timer, and nothing public starts it — and deferring the call
    // past the release did not help either, because the list is then shown with the
    // combo still unfocused and closes on the activation change instead. Which is
    // why clicking the arrow once cured it for the rest of the session, and why the
    // fix is to take that same path every time.
    QStyleOptionComboBox opt;
    opt.initFrom(combo);
    const QPoint arrow =
        combo->style()
            ->subControlRect(QStyle::CC_ComboBox, &opt,
                             QStyle::SC_ComboBoxArrow, combo)
            .center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(arrow),
                      QPointF(combo->mapToGlobal(arrow)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(combo, &press);
    return true;
  }

  // The spell-check language list is a multi-select and a dictionary manager: a
  // click on a row toggles that language's tick and keeps the list open (instead of
  // picking one entry and closing, which is what a plain combo does), and a click
  // on the row's button downloads or deletes that language's dictionary.
  if (ui->spellCheckLanguageComboBox->view() &&
      obj == ui->spellCheckLanguageComboBox->view()->viewport() &&
      (event->type() == QEvent::MouseButtonRelease ||
       event->type() == QEvent::MouseMove)) {
    QAbstractItemView *view = ui->spellCheckLanguageComboBox->view();
    if (event->type() == QEvent::MouseMove) {
      // The button lights up under the pointer, and moving within one row is a
      // move the view would not repaint for on its own.
      view->viewport()->update();
      return QWidget::eventFilter(obj, event);
    }
    auto *me = static_cast<QMouseEvent *>(event);
    const QModelIndex index = view->indexAt(me->pos());
    if (index.isValid() && index.data(DictionaryRows::CodeRole).toString().isEmpty()) {
      // The row that says the list of downloadable languages could not be
      // fetched: it stands for no language, so clicking it anywhere retries.
      m_spellRowsSyncing = true;
      view->model()->setData(index, tr("Fetching the list of languages…"),
                             Qt::DisplayRole);
      m_spellRowsSyncing = false;
      m_dictCatalogTries = 0;
      fetchDictionaryCatalog();
      return true;
    }
    if (index.isValid()) {
      const auto action = static_cast<DictionaryRows::Action>(
          index.data(DictionaryRows::ActionRole).toInt());
      if (action != DictionaryRows::Action::None &&
          DictionaryRowDelegate::actionRect(view->visualRect(index))
              .contains(me->pos())) {
        const QString code = index.data(DictionaryRows::CodeRole).toString();
        if (action == DictionaryRows::Action::Download)
          downloadDictionary(code);
        else
          deleteDictionary(code);
      } else if (index.data(DictionaryRows::InstalledRole).toBool()) {
        // Only a language that is here can be ticked; a row waiting to be
        // downloaded has nothing to check spelling with yet.
        const Qt::CheckState now =
            view->model()->data(index, Qt::CheckStateRole).value<Qt::CheckState>();
        view->model()->setData(index, now == Qt::Checked ? Qt::Unchecked
                                                          : Qt::Checked,
                               Qt::CheckStateRole);
      }
    }
    return true; // consume, so the popup does not close
  }

  // Sliders, combo boxes and spin boxes have this filter installed so the wheel
  // cannot change their value under a passing pointer. Swallowing the event did
  // that, but it also stopped the page: a scroll started anywhere died the moment
  // the pointer crossed one of them, so reaching the bottom of Settings meant
  // steering around every control on the way. Scroll the page by hand instead, so
  // the gesture stays with what it started on and the control still ignores it.
  //
  // Done by moving the scrollbar rather than re-sending the event: the viewport is
  // also a child of this widget, so forwarding would come straight back here.
  if (event->type() == QEvent::Wheel && isChildOf(this, obj)) {
    auto *wheel = static_cast<QWheelEvent *>(event);
    auto *target = qobject_cast<QWidget *>(obj);

    // An open drop-down list is a window of its own, floating above the page.
    // Scrolling the page under it left the list hanging in mid-air over settings
    // it has nothing to do with — the wheel over a list belongs to the list. The
    // spell-check picker reached this because its viewport carries this filter
    // for the multi-select clicks above.
    if (target && target->window() != this->window())
      return QWidget::eventFilter(obj, event);

    // A mouse wheel has no begin/end phase, so a gesture is however many notches
    // arrive without a pause, and whatever the first one chose keeps the rest of
    // them: a page scroll must not park itself the moment the pointer crosses a
    // list, and a list being scrolled must not hand the page a stray notch when
    // it reaches its end.
    if (!m_wheelIdle.isValid() || m_wheelIdle.hasExpired(400))
      m_wheelScrollsPage = !hasScrollOfItsOwn(target, wheel->angleDelta().y());
    m_wheelIdle.restart();
    if (!m_wheelScrollsPage)
      return QWidget::eventFilter(obj, event);

    QScrollBar *bar = ui->scrollArea ? ui->scrollArea->verticalScrollBar()
                                     : nullptr;
    const int dy = wheel->angleDelta().y();
    if (bar && dy != 0) {
      // Three steps per notch (a notch is 120), matching what the scroll area
      // does on its own, and keeping the division last so a high-resolution
      // wheel's smaller deltas are not rounded away to nothing.
      bar->setValue(bar->value() - dy * bar->singleStep() * 3 / 120);
    }
    return true;
  }
  return QWidget::eventFilter(obj, event);
}

void SettingsWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  wrapLongTooltips();
}

void SettingsWidget::wrapLongTooltips() {
  // Two thirds of the window, with a floor so a very narrow Settings window does
  // not produce a one-word-per-line column.
  const int wrapAt = qMax(360, width() * 2 / 3);
  const auto widgets = findChildren<QWidget *>();
  for (QWidget *w : widgets) {
    // The original plain text is kept on the widget, so re-wrapping after a
    // resize starts from the source rather than from the previous wrapping.
    QVariant stored = w->property("whatlyPlainTip");
    QString plain = stored.isValid() ? stored.toString() : w->toolTip();
    if (plain.isEmpty() || plain.startsWith(QLatin1Char('<')))
      continue; // nothing to do, or someone already made it rich text
    if (plain.length() < 60 && !stored.isValid())
      continue; // short enough to sit on one line
    if (!stored.isValid())
      w->setProperty("whatlyPlainTip", plain);
    // A width on a one-cell table is the reliable way to constrain wrapping in
    // Qt's rich text; a width on a div is not honoured consistently.
    //
    // cellpadding is the clearance. Qt places a tooltip that will not fit below
    // the pointer above it instead, and clamps it to the screen — so it is never
    // cut off, but it does end up flush against the edge. Padding inside the box
    // cannot move the box, and does keep the text itself off the screen edge,
    // which is the part that reads as cramped. Replacing Qt's tooltip outright to
    // gain a few pixels of outer margin is not worth it.
    w->setToolTip(
        QStringLiteral("<qt><table width=\"%1\" cellpadding=\"6\" "
                       "cellspacing=\"0\" border=\"0\">"
                       "<tr><td>%2</td></tr></table></qt>")
            .arg(wrapAt)
            .arg(plain.toHtmlEscaped()));
  }
}

void SettingsWidget::closeEvent(QCloseEvent *event) {
  SettingsManager::instance().settings().setValue("settingsGeo",
                                                  this->saveGeometry());
  QWidget::closeEvent(event);
}

bool SettingsWidget::hasScrollOfItsOwn(QWidget *target, int angleDeltaY) const {
  for (QWidget *w = target; w && w != ui->scrollArea; w = w->parentWidget()) {
    auto *area = qobject_cast<QAbstractScrollArea *>(w);
    if (!area)
      continue;
    const QScrollBar *bar = area->verticalScrollBar();
    if (angleDeltaY > 0 && bar->value() > bar->minimum())
      return true;
    if (angleDeltaY < 0 && bar->value() < bar->maximum())
      return true;
    // Already at that end, so it has nothing left to give: the page takes the
    // gesture instead of the notch being lost. Only for a gesture that starts
    // here — one that scrolls a list down to its end keeps the list until the
    // hand stops, rather than running on into the page.
  }
  return false;
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
  ui->cacheSize->setText(
      StorageInfo::humanReadable(StorageInfo::directorySize(cachePath())));
}

void SettingsWidget::loadShortcuts() {
  auto *host = ui->shortcutsFormHost;
  if (!host || host->layout())
    return; // build once
  auto *form = new QFormLayout(host);
  form->setContentsMargins(0, 0, 0, 0);
  for (const Shortcuts::Def &d : Shortcuts::registered()) {
    auto *edit = new QKeySequenceEdit(Shortcuts::get(d.id), host);
    const QString id = d.id;
    connect(edit, &QKeySequenceEdit::editingFinished, this, [this, edit, id]() {
      const QKeySequence seq = edit->keySequence();
      const QString clash = Shortcuts::conflictId(id, seq);
      if (!clash.isEmpty()) {
        QMessageBox::warning(
            this, tr("Shortcut in use"),
            tr("That shortcut is already used by another action."));
        edit->setKeySequence(Shortcuts::get(id)); // revert
        return;
      }
      Shortcuts::set(id, seq);
    });
    form->addRow(d.label, edit);
  }
}

void SettingsWidget::on_clearCacheButton_clicked() {
  if (QMessageBox::question(
          this, tr("Clear cache"),
          tr("Clear the cache now? It will be re-downloaded as needed.")) !=
      QMessageBox::Yes)
    return;
  if (Utils::delete_cache(cachePath()))
    ui->cacheSize->setText(
        StorageInfo::humanReadable(StorageInfo::directorySize(cachePath())));
}

void SettingsWidget::on_exportProfileButton_clicked() {
  if (QMessageBox::warning(
          this, tr("Export profile"),
          tr("The archive will contain your logged-in WhatsApp session. Keep "
             "it private. Continue?"),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    return;
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export profile"),
      QStringLiteral("whatly-profile.tar.gz"),
      tr("Archives (*.tar.gz)"));
  if (path.isEmpty())
    return;
  if (!path.endsWith(QLatin1String(".tar.gz")))
    path += QStringLiteral(".tar.gz");
  QString error;
  if (Backup::exportProfile(path, &error))
    QMessageBox::information(this, tr("Export profile"),
                             tr("Profile exported."));
  else
    QMessageBox::warning(this, tr("Export profile"), error);
}

void SettingsWidget::on_importProfileButton_clicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Import profile"), QString(), tr("Archives (*.tar.gz)"));
  if (path.isEmpty())
    return;
  if (QMessageBox::warning(
          this, tr("Import profile"),
          tr("This overwrites the current account's data with the archive, "
             "then Whatly must be restarted. Continue?"),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    return;
  QString error;
  if (Backup::importProfile(path, &error))
    QMessageBox::information(
        this, tr("Import profile"),
        tr("Profile imported. Please restart Whatly."));
  else
    QMessageBox::warning(this, tr("Import profile"), error);
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

void SettingsWidget::on_notificationSoundCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("notificationSound", checked);
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
  // Never reconstruct/show the passcode (stored hashed, issue #42); only show
  // whether one is set.
  this->setCurrentPasswordText(
      SettingsManager::instance().settings().value("asdfg").isValid()
          ? QString()
          : QStringLiteral("Require setup"));
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
  ui->current_password->setReadOnly(true);
  if (str == "Require setup") {
    ui->current_password->setEchoMode(QLineEdit::Normal);
    ui->current_password->setText(tr("Require setup"));
    ui->viewPassword->setVisible(false);
  } else {
    // The passcode is stored hashed and cannot (and must not) be shown, so the
    // field is only a fixed mask indicating one is set, and the reveal button is
    // gone (issue #42).
    ui->current_password->setEchoMode(QLineEdit::Password);
    ui->current_password->setText(QStringLiteral("passwordset"));
    ui->viewPassword->setVisible(false);
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
  if (checked) // needs a tray icon — see on_hideTrayIconCheckBox_toggled
    ui->hideTrayIconCheckBox->setChecked(false);
}

void SettingsWidget::on_rememberWindowLayoutCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("rememberWindowLayout", checked);
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
  // There is nothing to start minimised into, and nothing to click, once the
  // tray icon is gone — so turning this on quietly turns those two off instead
  // of leaving settings that silently do nothing. Each of them turns this one
  // back off the same way; the chain stops because only enabling excludes.
  if (checked) {
    ui->startMinimized->setChecked(false);
    ui->minimizeOnTrayIconClick->setChecked(false);
  }
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

void SettingsWidget::on_lockOnScreenLockCheckBox_toggled(bool checked) {
  ScreenLock::setEnabled(checked);
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

  // Font-hinting combo: item data is the stored token ("" = follow the system).
  if (ui->fontHintingComboBox->count() == 0) {
    ui->fontHintingComboBox->blockSignals(true);
    ui->fontHintingComboBox->addItem(tr("Automatic"), QString());
    ui->fontHintingComboBox->addItem(tr("None"), QStringLiteral("none"));
    ui->fontHintingComboBox->addItem(tr("Slight"), QStringLiteral("slight"));
    ui->fontHintingComboBox->addItem(tr("Medium"), QStringLiteral("medium"));
    ui->fontHintingComboBox->addItem(tr("Full"), QStringLiteral("full"));
    ui->fontHintingComboBox->blockSignals(false);
  }
  {
    const int idx =
        ui->fontHintingComboBox->findData(Performance::fontHinting());
    ui->fontHintingComboBox->blockSignals(true);
    ui->fontHintingComboBox->setCurrentIndex(idx < 0 ? 0 : idx);
    ui->fontHintingComboBox->blockSignals(false);
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
  set(ui->optimizeForSizeCheckBox, Performance::optimizeForSize());
  set(ui->webrtcShieldCheckBox, Performance::webrtcShield());
  set(ui->focusModeCheckBox, FocusMode::isEnabled());
  set(ui->hdMediaCheckBox, HdMedia::isEnabled());
  set(ui->undoSendCheckBox, UndoSend::isEnabled());
  ui->undoSendSecondsSpinBox->blockSignals(true);
  ui->undoSendSecondsSpinBox->setValue(UndoSend::seconds());
  ui->undoSendSecondsSpinBox->blockSignals(false);
  set(ui->translateEnabledCheckBox, Translate::isEnabled());
  ui->translateEndpointLineEdit->setText(Translate::endpoint());
  ui->translateApiKeyLineEdit->setText(Translate::apiKey());
  ui->translateTargetLineEdit->setText(Translate::targetLang());
  set(ui->aiEnabledCheckBox, Ai::isEnabled());
  ui->aiEndpointLineEdit->setText(Ai::endpoint());
  ui->aiModelLineEdit->setText(Ai::model());
  ui->aiApiKeyLineEdit->setText(Ai::apiKey());
  if (ui->aiRecommendedCombo->count() == 0) {
    for (const Ollama::RecModel &m : Ollama::recommendedModels())
      ui->aiRecommendedCombo->addItem(
          QStringLiteral("%1  (%2): %3").arg(m.name, m.size, m.note), m.name);
  }
  set(ui->suspendInactiveAccountsCheckBox,
      Performance::suspendInactiveAccounts());
  ui->suspendAfterSpinBox->blockSignals(true);
  ui->suspendAfterSpinBox->setValue(Performance::suspendAfterMinutes());
  ui->suspendAfterSpinBox->blockSignals(false);
  set(ui->unloadOffscreenWindowsCheckBox, Performance::unloadOffscreenWindows());
  // Greyed out rather than hidden while unloading is off: the option has to be
  // readable to be understood, and it does nothing on its own.
  ui->unloadOffscreenWindowsCheckBox->setEnabled(
      Performance::suspendInactiveAccounts());

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
void SettingsWidget::on_optimizeForSizeCheckBox_toggled(bool checked) {
  Performance::setOptimizeForSize(checked);
}
void SettingsWidget::on_webrtcShieldCheckBox_toggled(bool checked) {
  Performance::setWebrtcShield(checked);
}

void SettingsWidget::on_focusModeCheckBox_toggled(bool checked) {
  FocusMode::setEnabled(checked);
  emit focusModeChanged();
}

void SettingsWidget::on_hdMediaCheckBox_toggled(bool checked) {
  HdMedia::setEnabled(checked);
  emit hdMediaChanged();
}
void SettingsWidget::on_undoSendCheckBox_toggled(bool checked) {
  UndoSend::setEnabled(checked);
  emit undoSendChanged();
}
void SettingsWidget::on_undoSendSecondsSpinBox_valueChanged(int arg1) {
  UndoSend::setSeconds(arg1);
  emit undoSendChanged();
}
void SettingsWidget::on_translateEnabledCheckBox_toggled(bool checked) {
  Translate::setEnabled(checked);
}
void SettingsWidget::on_translateEndpointLineEdit_editingFinished() {
  Translate::setEndpoint(ui->translateEndpointLineEdit->text());
}
void SettingsWidget::on_translateApiKeyLineEdit_editingFinished() {
  Translate::setApiKey(ui->translateApiKeyLineEdit->text());
}
void SettingsWidget::on_translateTargetLineEdit_editingFinished() {
  Translate::setTargetLang(ui->translateTargetLineEdit->text());
}
void SettingsWidget::on_aiEnabledCheckBox_toggled(bool checked) {
  Ai::setEnabled(checked);
}
void SettingsWidget::on_aiEndpointLineEdit_editingFinished() {
  Ai::setEndpoint(ui->aiEndpointLineEdit->text());
}
void SettingsWidget::on_aiModelLineEdit_editingFinished() {
  Ai::setModel(ui->aiModelLineEdit->text());
}
void SettingsWidget::on_aiApiKeyLineEdit_editingFinished() {
  Ai::setApiKey(ui->aiApiKeyLineEdit->text());
}

void SettingsWidget::on_aiDetectButton_clicked() {
  if (!m_ollama) {
    m_ollama = new OllamaManager(this);
    connect(m_ollama, &OllamaManager::checked, this,
            [this](bool available, const QStringList &models) {
              if (!available) {
                ui->aiOllamaStatusLabel->setText(
                    tr("Ollama not found at this address."));
                ui->aiInstalledModelsCombo->clear();
                return;
              }
              ui->aiOllamaStatusLabel->setText(
                  tr("Ollama found (%1 models installed).")
                      .arg(models.size()));
              ui->aiInstalledModelsCombo->clear();
              ui->aiInstalledModelsCombo->addItems(models);
              const int i = ui->aiInstalledModelsCombo->findText(Ai::model());
              if (i >= 0)
                ui->aiInstalledModelsCombo->setCurrentIndex(i);
            });
    connect(m_ollama, &OllamaManager::pullProgress, this,
            [this](int percent, const QString &status) {
              ui->aiPullProgressBar->setVisible(true);
              if (percent >= 0) {
                ui->aiPullProgressBar->setRange(0, 100);
                ui->aiPullProgressBar->setValue(percent);
              } else {
                ui->aiPullProgressBar->setRange(0, 0); // busy/indeterminate
              }
              if (!status.isEmpty())
                ui->aiPullStatusLabel->setText(status);
            });
    connect(m_ollama, &OllamaManager::pullFinished, this,
            [this](bool ok, const QString &error) {
              ui->aiDownloadButton->setEnabled(true);
              ui->aiPullProgressBar->setVisible(false);
              if (ok) {
                ui->aiPullStatusLabel->setText(tr("Download complete."));
                on_aiDetectButton_clicked(); // refresh the installed list
              } else {
                ui->aiPullStatusLabel->setText(tr("Download failed: %1").arg(error));
              }
            });
  }
  // If the endpoint is empty, default it to a local Ollama so detection (and
  // later use) works out of the box.
  if (ui->aiEndpointLineEdit->text().trimmed().isEmpty()) {
    ui->aiEndpointLineEdit->setText(
        QStringLiteral("http://localhost:11434/v1/chat/completions"));
    Ai::setEndpoint(ui->aiEndpointLineEdit->text());
  }
  ui->aiOllamaStatusLabel->setText(tr("Checking…"));
  m_ollama->check(Ollama::baseUrl(ui->aiEndpointLineEdit->text()));
}

void SettingsWidget::on_aiInstalledModelsCombo_activated(int index) {
  const QString name = ui->aiInstalledModelsCombo->itemText(index);
  if (name.isEmpty())
    return;
  ui->aiModelLineEdit->setText(name);
  Ai::setModel(name);
}

void SettingsWidget::on_aiDownloadButton_clicked() {
  const QString model =
      ui->aiRecommendedCombo->currentData().toString();
  if (model.isEmpty() || !m_ollama) {
    // Make sure the manager exists (its signals are wired in the detect slot).
    on_aiDetectButton_clicked();
  }
  if (ui->aiRecommendedCombo->currentData().toString().isEmpty())
    return;
  const QString chosen = ui->aiRecommendedCombo->currentData().toString();
  ui->aiDownloadButton->setEnabled(false);
  ui->aiPullProgressBar->setVisible(true);
  ui->aiPullProgressBar->setRange(0, 0);
  ui->aiPullStatusLabel->setText(tr("Starting download of %1…").arg(chosen));
  m_ollama->pull(Ollama::baseUrl(ui->aiEndpointLineEdit->text()), chosen);
}
void SettingsWidget::on_jsMemoryLimitSpinBox_valueChanged(int arg1) {
  Performance::setJsMemoryLimitMb(arg1);
}
void SettingsWidget::on_cacheTypeComboBox_currentIndexChanged(int index) {
  Performance::setCacheType(
      ui->cacheTypeComboBox->itemData(index).toString());
}
void SettingsWidget::on_suspendInactiveAccountsCheckBox_toggled(bool checked) {
  Performance::setSuspendInactiveAccounts(checked);
  ui->unloadOffscreenWindowsCheckBox->setEnabled(checked);
}
void SettingsWidget::on_unloadOffscreenWindowsCheckBox_toggled(bool checked) {
  Performance::setUnloadOffscreenWindows(checked);
}
void SettingsWidget::on_suspendAfterSpinBox_valueChanged(int arg1) {
  Performance::setSuspendAfterMinutes(arg1);
}
void SettingsWidget::on_fontHintingComboBox_currentIndexChanged(int index) {
  // Applied as a Chromium flag at start-up, so it takes effect after a restart.
  Performance::setFontHinting(
      ui->fontHintingComboBox->itemData(index).toString());
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

  ui->alwaysShowAccountTabsCheckBox->blockSignals(true);
  ui->alwaysShowAccountTabsCheckBox->setChecked(
      MainWindow::alwaysShowAccountTabs());
  ui->alwaysShowAccountTabsCheckBox->blockSignals(false);

  updateTitleBarOptionState(); // both window-frame boxes

  ui->checkUpdatesCheckBox->blockSignals(true);
  ui->checkUpdatesCheckBox->setChecked(UpdateChecker::isEnabled());
  ui->checkUpdatesCheckBox->blockSignals(false);

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

void SettingsWidget::loadCloudApiSettings() {
  ui->cloudPhoneIdEdit->setText(CloudApi::phoneNumberId());
  ui->cloudTokenEdit->setText(CloudApi::accessToken());
  ui->cloudApiVersionEdit->setText(CloudApi::apiVersion());
}

void SettingsWidget::on_cloudPhoneIdEdit_editingFinished() {
  CloudApi::setPhoneNumberId(ui->cloudPhoneIdEdit->text());
}

void SettingsWidget::on_cloudTokenEdit_editingFinished() {
  CloudApi::setAccessToken(ui->cloudTokenEdit->text());
}

void SettingsWidget::on_cloudApiVersionEdit_editingFinished() {
  CloudApi::setApiVersion(ui->cloudApiVersionEdit->text());
}

void SettingsWidget::loadLocalApiSettings() {
  ui->localApiEnabledCheckBox->blockSignals(true);
  ui->localApiEnabledCheckBox->setChecked(LocalApi::isEnabled());
  ui->localApiEnabledCheckBox->blockSignals(false);
  ui->localApiPortSpinBox->blockSignals(true);
  ui->localApiPortSpinBox->setValue(LocalApi::port());
  ui->localApiPortSpinBox->blockSignals(false);
  ui->localApiTokenEdit->setText(LocalApi::token());

  ui->webhookEnabledCheckBox->blockSignals(true);
  ui->webhookEnabledCheckBox->setChecked(CloudWebhook::isEnabled());
  ui->webhookEnabledCheckBox->blockSignals(false);
  ui->webhookVerifyTokenEdit->setText(CloudWebhook::verifyToken());
  ui->webhookAppSecretEdit->setText(CloudWebhook::appSecret());
}

void SettingsWidget::on_localApiEnabledCheckBox_toggled(bool checked) {
  LocalApi::setEnabled(checked);
  emit localApiSettingsChanged();
}

void SettingsWidget::on_localApiPortSpinBox_valueChanged(int value) {
  LocalApi::setPort(value);
  emit localApiSettingsChanged();
}

void SettingsWidget::on_localApiTokenEdit_editingFinished() {
  LocalApi::setToken(ui->localApiTokenEdit->text());
  emit localApiSettingsChanged();
}

void SettingsWidget::on_webhookEnabledCheckBox_toggled(bool checked) {
  CloudWebhook::setEnabled(checked);
  emit localApiSettingsChanged();
}

void SettingsWidget::on_webhookVerifyTokenEdit_editingFinished() {
  CloudWebhook::setVerifyToken(ui->webhookVerifyTokenEdit->text());
  emit localApiSettingsChanged();
}

void SettingsWidget::on_webhookAppSecretEdit_editingFinished() {
  CloudWebhook::setAppSecret(ui->webhookAppSecretEdit->text());
  emit localApiSettingsChanged();
}

void SettingsWidget::loadNotificationRules() {
  ui->dndCheckBox->blockSignals(true);
  ui->dndCheckBox->setChecked(NotificationRules::dndEnabled());
  ui->dndCheckBox->blockSignals(false);

  const QString fmt = QStringLiteral("HH:mm");
  ui->dndStartTimeEdit->blockSignals(true);
  ui->dndStartTimeEdit->setTime(QTime::fromString(NotificationRules::dndStart(), fmt));
  ui->dndStartTimeEdit->blockSignals(false);
  ui->dndEndTimeEdit->blockSignals(true);
  ui->dndEndTimeEdit->setTime(QTime::fromString(NotificationRules::dndEnd(), fmt));
  ui->dndEndTimeEdit->blockSignals(false);

  ui->keywordsLineEdit->setText(NotificationRules::keywords().join(QStringLiteral(", ")));
  ui->vipContactsLineEdit->setText(
      NotificationRules::vipContacts().join(QStringLiteral(", ")));
  ui->mutedContactsLineEdit->setText(
      NotificationRules::mutedContacts().join(QStringLiteral(", ")));

  ui->inlineReplyCheckBox->blockSignals(true);
  ui->inlineReplyCheckBox->setChecked(NotificationRules::inlineReplyEnabled());
  ui->inlineReplyCheckBox->blockSignals(false);

  const bool on = NotificationRules::dndEnabled();
  ui->dndStartTimeEdit->setEnabled(on);
  ui->dndEndTimeEdit->setEnabled(on);
}

void SettingsWidget::on_dndCheckBox_toggled(bool checked) {
  NotificationRules::setDndEnabled(checked);
  ui->dndStartTimeEdit->setEnabled(checked);
  ui->dndEndTimeEdit->setEnabled(checked);
}

void SettingsWidget::on_dndStartTimeEdit_timeChanged(const QTime &t) {
  NotificationRules::setDndStart(t.toString(QStringLiteral("HH:mm")));
}

void SettingsWidget::on_dndEndTimeEdit_timeChanged(const QTime &t) {
  NotificationRules::setDndEnd(t.toString(QStringLiteral("HH:mm")));
}

void SettingsWidget::on_keywordsLineEdit_editingFinished() {
  const QStringList words =
      ui->keywordsLineEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
  QStringList cleaned;
  for (const QString &w : words) {
    const QString t = w.trimmed();
    if (!t.isEmpty())
      cleaned << t;
  }
  NotificationRules::setKeywords(cleaned);
}

void SettingsWidget::on_vipContactsLineEdit_editingFinished() {
  NotificationRules::setVipContacts(
      ui->vipContactsLineEdit->text().split(QLatin1Char(','),
                                            Qt::SkipEmptyParts));
}

void SettingsWidget::on_mutedContactsLineEdit_editingFinished() {
  NotificationRules::setMutedContacts(
      ui->mutedContactsLineEdit->text().split(QLatin1Char(','),
                                              Qt::SkipEmptyParts));
}

void SettingsWidget::on_inlineReplyCheckBox_toggled(bool checked) {
  NotificationRules::setInlineReplyEnabled(checked);
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

void SettingsWidget::refreshCannedList() {
  ui->cannedList->clear();
  for (const CannedResponses::Response &r : CannedResponses::all())
    ui->cannedList->addItem(r.title);
  ui->removeCannedButton->setEnabled(ui->cannedList->count() > 0);
}

void SettingsWidget::on_addCannedButton_clicked() {
  bool ok = false;
  const QString title = QInputDialog::getText(
      this, tr("Add reply"), tr("Name"), QLineEdit::Normal, QString(), &ok);
  if (!ok || title.trimmed().isEmpty())
    return;
  const QString text = QInputDialog::getMultiLineText(
      this, tr("Add reply"), tr("Text to insert"), QString(), &ok);
  if (!ok || text.isEmpty())
    return;
  CannedResponses::add(title, text);
  refreshCannedList();
}

void SettingsWidget::on_removeCannedButton_clicked() {
  const int row = ui->cannedList->currentRow();
  if (row < 0)
    return;
  CannedResponses::removeAt(row);
  refreshCannedList();
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

// Hiding the title bar means drawing the window's chrome ourselves, so it needs
// the custom frame. Rather than greying the option out until the user has found
// and understood the other one — which reads as the app refusing a setting for
// no stated reason — turning this on turns that on too.
void SettingsWidget::updateTitleBarOptionState() {
  ui->customWindowFrameCheckBox->blockSignals(true);
  ui->customWindowFrameCheckBox->setChecked(CustomTitleBar::isEnabled());
  ui->customWindowFrameCheckBox->blockSignals(false);
  ui->tabsInTitleBarCheckBox->blockSignals(true);
  ui->tabsInTitleBarCheckBox->setChecked(CustomTitleBar::tabsInTitleBar());
  ui->tabsInTitleBarCheckBox->blockSignals(false);
}

void SettingsWidget::on_customWindowFrameCheckBox_toggled(bool checked) {
  CustomTitleBar::setEnabled(checked);
  // Switching the frame off leaves the stored "hide the title bar" value alone,
  // so switching it back on restores what the user actually chose — but the
  // checkbox has to stop claiming a hidden title bar in the meantime.
  updateTitleBarOptionState();
}

void SettingsWidget::on_tabsInTitleBarCheckBox_toggled(bool checked) {
  CustomTitleBar::setTabsInTitleBar(checked);
  if (checked)
    CustomTitleBar::setEnabled(true);
  updateTitleBarOptionState();
}

void SettingsWidget::on_alwaysShowAccountTabsCheckBox_toggled(bool checked) {
  MainWindow::setAlwaysShowAccountTabs(checked);
  // Unlike the frame settings this one needs no restart: the strip is just
  // shown or hidden.
  if (auto *w = qobject_cast<MainWindow *>(parent()))
    w->refreshAccountStrip();
}

void SettingsWidget::on_restartNowButton_clicked() { emit restartRequested(); }

// The accordion headers, in the order they appear. They are the QToolButtons
// carrying an arrow — the same handle the section test uses — so there is no
// second list to keep in step with the one that builds them.
static QList<QToolButton *> sectionHeaders(const QWidget *page) {
  QList<QToolButton *> headers;
  for (auto *tb : page->findChildren<QToolButton *>())
    if (tb->arrowType() == Qt::DownArrow || tb->arrowType() == Qt::RightArrow)
      headers << tb;
  return headers;
}

void SettingsWidget::saveUiState() {
  QSettings &s = SettingsManager::instance().settings();
  QStringList open;
  const QList<QToolButton *> headers = sectionHeaders(this);
  for (int i = 0; i < headers.size(); ++i)
    if (headers[i]->isChecked())
      open << QString::number(i);
  s.setValue(QStringLiteral("ui/settingsSections"), open.join(QLatin1Char(',')));
  s.setValue(QStringLiteral("ui/settingsScroll"),
             ui->scrollArea->verticalScrollBar()->value());
  s.setValue(QStringLiteral("ui/settingsGeometry"), saveGeometry());
  s.setValue(QStringLiteral("ui/settingsWasOpen"), isVisible());
  // Which build wrote it. The sections are stored by position in the accordion,
  // which is only a name for the same section while the accordion is the same:
  // a version that adds one, or reorders them, would have this replayed onto a
  // different list and open the wrong rows.
  s.setValue(QStringLiteral("ui/settingsUiVersion"),
             QString::fromLatin1(VERSIONSTR));
}

void SettingsWidget::restoreUiState() {
  QSettings &s = SettingsManager::instance().settings();
  const QByteArray geom =
      s.value(QStringLiteral("ui/settingsGeometry")).toByteArray();
  if (!geom.isEmpty())
    restoreGeometry(geom);

  // A window's size and place mean the same thing in any version, so they are
  // restored above whatever wrote them. Positions in a list are not: replay them
  // only for the build that wrote them, and after an upgrade let the page open
  // the way a first launch does.
  const bool sameBuild = s.value(QStringLiteral("ui/settingsUiVersion"))
                             .toString() == QString::fromLatin1(VERSIONSTR);
  int offset = 0;
  if (sameBuild) {
    QSet<int> want;
    const QStringList open = s.value(QStringLiteral("ui/settingsSections"))
                                 .toString()
                                 .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &n : open)
      want.insert(n.toInt());
    const QList<QToolButton *> headers = sectionHeaders(this);
    for (int i = 0; i < headers.size(); ++i)
      headers[i]->setChecked(want.contains(i));
    offset = s.value(QStringLiteral("ui/settingsScroll"), 0).toInt();
  }

  // Written for one restart and read by it: consumed here, so nothing is left in
  // the file to be replayed by a later launch that never asked for it. The values
  // above are already in hand, the one below by copy.
  for (const QString &key :
       {QStringLiteral("ui/settingsSections"), QStringLiteral("ui/settingsScroll"),
        QStringLiteral("ui/settingsGeometry"),
        QStringLiteral("ui/settingsUiVersion")})
    s.remove(key);

  // The sections have only just been told to open; the scroll range they
  // create does not exist until the layout settles, so the offset is applied
  // one turn of the event loop later.
  QTimer::singleShot(0, this, [this, offset]() {
    ui->scrollArea->verticalScrollBar()->setValue(offset);
  });
}

void SettingsWidget::on_checkUpdatesCheckBox_toggled(bool checked) {
  UpdateChecker::setEnabled(checked);
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

  syncSpellCheckRows();
}

// The picker's list, which is also where dictionaries are got and got rid of
// (issue #46): one row per language, carrying the tick that says spelling is
// checked against it and the one button its state allows. See DictionaryRows for
// what a row is, DictionaryRowDelegate for how it is drawn, and eventFilter() for
// the click that presses its button.
void SettingsWidget::syncSpellCheckRows() {
  auto *combo = ui->spellCheckLanguageComboBox;
  combo->blockSignals(true);
  if (!combo->isEditable()) {
    combo->setEditable(true);
    combo->lineEdit()->setReadOnly(true);
    combo->lineEdit()->setFocusPolicy(Qt::NoFocus);
    // Being editable is what lets it show a summary instead of one entry's text,
    // and it is also why clicking the box did nothing: an editable combo opens its
    // list from the arrow alone. See eventFilter().
    combo->lineEdit()->installEventFilter(this);
  }
  if (!m_spellRows) {
    m_spellRows = new QStandardItemModel(combo);
    combo->setModel(m_spellRows);
    connect(m_spellRows, &QStandardItemModel::itemChanged, this,
            [this](QStandardItem *) {
              if (m_spellRowsSyncing)
                return; // this refresh's own writing, not the user ticking
              saveSpellCheckLanguages();
            });
    if (QAbstractItemView *view = combo->view()) {
      view->setItemDelegate(new DictionaryRowDelegate(view));
      // The clicks: a tick that must not dismiss the list, and the row's button.
      view->viewport()->installEventFilter(this);
    }
  }

  const QStringList installed = Dictionaries::availableDictionaries();
  QStringList removable;
  for (const QString &code : installed)
    if (DictionaryManager::isRemovable(code))
      removable << code; // a real download, not a symlink into the bundle
  const QStringList selected = Dictionaries::selectedDictionaries();
  const QList<DictionaryRows::Row> rows =
      DictionaryRows::build(installed, removable, m_dictCatalog);

  // Updated in place rather than rebuilt: this list is usually open while a
  // download finishes on it or a language is deleted from it, and handing the
  // combo a new model would pull the list out from under the pointer. Rows are
  // matched by their code, so a row that appears or goes finds its own place.
  m_spellRowsSyncing = true;
  for (int i = 0; i < rows.size(); ++i) {
    const DictionaryRows::Row &row = rows.at(i);
    int at = -1;
    for (int j = i; j < m_spellRows->rowCount(); ++j)
      if (m_spellRows->item(j)->data(DictionaryRows::CodeRole).toString() ==
          row.code) {
        at = j;
        break;
      }
    QStandardItem *item = nullptr;
    if (at < 0) {
      item = new QStandardItem;
      m_spellRows->insertRow(i, item);
    } else {
      if (at != i)
        m_spellRows->insertRow(i, m_spellRows->takeRow(at));
      item = m_spellRows->item(i);
    }

    // A download in flight keeps its per-cent, and one that failed keeps saying so
    // until it is tried again or the language arrives some other way.
    const QVariant was = item->data(DictionaryRows::ProgressRole);
    int progress = DictionaryRows::Idle;
    if (m_dictManager && m_dictManager->isDownloading(row.code))
      progress = was.isValid() ? was.toInt() : 0;
    else if (!row.installed && was.toInt() == DictionaryRows::Failed)
      progress = DictionaryRows::Failed;

    item->setData(row.label, Qt::DisplayRole);
    item->setData(row.code, DictionaryRows::CodeRole);
    item->setData(row.installed, DictionaryRows::InstalledRole);
    item->setData(static_cast<int>(row.action), DictionaryRows::ActionRole);
    item->setData(row.downloadSize, DictionaryRows::SizeRole);
    item->setData(progress, DictionaryRows::ProgressRole);
    if (progress != DictionaryRows::Failed) // that one carries the reason
      item->setData(DictionaryRows::tooltip(row), Qt::ToolTipRole);
    item->setCheckState(row.installed && selected.contains(row.code)
                            ? Qt::Checked
                            : Qt::Unchecked);
    // A language that is not on disk cannot be checked against, so its row is
    // greyed and its download arrow is the only thing on it that does anything.
    item->setFlags(row.installed ? (Qt::ItemIsEnabled | Qt::ItemIsUserCheckable)
                                 : Qt::ItemIsUserCheckable);
  }
  while (m_spellRows->rowCount() > rows.size())
    m_spellRows->removeRow(m_spellRows->rowCount() - 1); // languages that went

  // Nothing downloadable, and a reason for it: one row saying so, which is also
  // the retry. Without it a failed fetch left a list of whatever is installed and
  // no hint that thirty more languages were meant to be in it.
  if (m_dictCatalog.isEmpty() && !m_dictCatalogError.isEmpty()) {
    auto *notice = new QStandardItem(
        tr("Downloadable languages unavailable — click to try again"));
    notice->setToolTip(m_dictCatalogError);
    notice->setData(QString(), DictionaryRows::CodeRole); // no language of its own
    notice->setData(false, DictionaryRows::InstalledRole);
    notice->setData(static_cast<int>(DictionaryRows::Action::None),
                    DictionaryRows::ActionRole);
    notice->setData(DictionaryRows::Idle, DictionaryRows::ProgressRole);
    notice->setFlags(Qt::ItemIsEnabled); // clickable, but nothing to tick
    m_spellRows->appendRow(notice);
  }
  m_spellRowsSyncing = false;

  // Reads as single-choice at a glance, but it is a multi-select and a dictionary
  // manager besides: say so, since neither shows until the list is open.
  combo->setToolTip(tr("Tick the languages to check spelling against. Each row "
                       "downloads or deletes its dictionary."));
  keepTheSpinnersTurning(); // a download that landed leaves nothing to turn

  // Live even when spelling is not being checked, and when nothing is installed at
  // all: this is where a dictionary is fetched, so locking it would leave whoever
  // has none no way to get one.
  combo->setEnabled(m_spellRows->rowCount() > 0);
  combo->blockSignals(false);
  updateSpellCheckSummary();
}

// A download's per-cent on the row it belongs to, or DictionaryRows::Failed with
// the reason on the tooltip. Nothing happens for a language the list is not
// showing, which is the case while Settings has never been opened.
void SettingsWidget::setSpellCheckRowProgress(const QString &code, int progress,
                                              const QString &error) {
  if (!m_spellRows)
    return;
  for (int i = 0; i < m_spellRows->rowCount(); ++i) {
    QStandardItem *item = m_spellRows->item(i);
    if (item->data(DictionaryRows::CodeRole).toString() != code)
      continue;
    m_spellRowsSyncing = true;
    item->setData(progress, DictionaryRows::ProgressRole);
    if (!error.isEmpty())
      item->setData(item->data(Qt::ToolTipRole).toString() +
                        QLatin1Char('\n') + error,
                    Qt::ToolTipRole);
    m_spellRowsSyncing = false;
    break;
  }
  keepTheSpinnersTurning();
}

// A spinner is only a spinner if something repaints it. Setting a row's data marks
// the view dirty once, which would leave a still arc for the whole download — so
// nudge the list while any row is waiting, and stop the moment none is.
void SettingsWidget::keepTheSpinnersTurning() {
  bool waiting = false;
  if (m_spellRows)
    for (int i = 0; i < m_spellRows->rowCount() && !waiting; ++i)
      waiting = m_spellRows->item(i)->data(DictionaryRows::ProgressRole).toInt() >= 0;

  if (!waiting) {
    if (m_spellSpin)
      m_spellSpin->stop();
    return;
  }
  if (!m_spellSpin) {
    m_spellSpin = new QTimer(this);
    m_spellSpin->setInterval(60); // about as fast as an eye reads movement
    connect(m_spellSpin, &QTimer::timeout, this, [this]() {
      QAbstractItemView *view = ui->spellCheckLanguageComboBox->view();
      if (view && view->isVisible())
        view->viewport()->update();
    });
  }
  if (!m_spellSpin->isActive())
    m_spellSpin->start();
}

// Ask for the list of downloadable languages, counting the attempt so a fetch that
// keeps failing stops retrying and says so instead.
void SettingsWidget::fetchDictionaryCatalog() {
  if (!m_dictManager)
    return;
  ++m_dictCatalogTries;
  m_dictManager->fetchCatalog();
}

// Fetch one language's dictionary, named by its row. Only a catalogued language
// can be fetched: the file has to come from somewhere, and the manifest is what
// says how big it is and what it should hash to.
void SettingsWidget::downloadDictionary(const QString &code) {
  if (!m_dictManager)
    return;
  for (const DictionaryEntry &entry : m_dictCatalog)
    if (entry.code == code) {
      setSpellCheckRowProgress(code, 0);
      m_dictManager->download(entry);
      return;
    }
}

// Delete one language's dictionary from disk. The tick is deliberately left where
// it was: the stored list keeps the language and selectedDictionaries() drops what
// is not on disk, so downloading it again restores the choice rather than making
// the user find it a second time.
void SettingsWidget::deleteDictionary(const QString &code) {
  if (!m_dictManager || !m_dictManager->remove(code))
    return;
  populateSpellCheck();
  emit spellCheckChanged();
}

// The combo shows a summary of what is checked rather than one entry's text.
void SettingsWidget::updateSpellCheckSummary() {
  const QStringList selected = Dictionaries::selectedDictionaries();
  QString text;
  const QString focus = Dictionaries::focusedDictionary();
  if (selected.isEmpty())
    text = tr("Choose languages\u2026"); // hints the picker takes several
  else if (selected.size() == 1)
    text = selected.first();
  else if (!focus.isEmpty())
    // Ticked three and checking against one of them: saying "3 languages" here
    // would be a half-truth, and the checker's behaviour the puzzling half.
    text = tr("%1 of %2 chosen").arg(focus).arg(selected.size());
  else
    text = tr("%1 languages").arg(selected.size());
  ui->spellCheckLanguageComboBox->setCurrentText(text);
  // A non-editable combo ignores setCurrentText, so also set it as the
  // placeholder-style display via the line edit if one exists.
  if (ui->spellCheckLanguageComboBox->lineEdit())
    ui->spellCheckLanguageComboBox->lineEdit()->setText(text);
}

void SettingsWidget::saveSpellCheckLanguages() {
  const QStringList installed = Dictionaries::availableDictionaries();
  const QStringList stored = SettingsManager::instance()
                                 .settings()
                                 .value(QStringLiteral("spellCheckLanguages"))
                                 .toStringList();
  const auto ticked = [this](const QString &code) {
    if (!m_spellRows)
      return false;
    for (int i = 0; i < m_spellRows->rowCount(); ++i)
      if (m_spellRows->item(i)->data(DictionaryRows::CodeRole).toString() == code)
        return m_spellRows->item(i)->checkState() == Qt::Checked;
    return false;
  };

  QStringList chosen;
  // What was already stored, in the order it was stored in, so ticking one box does
  // not shuffle the lap Ctrl+Alt+S walks. A language that is not on disk keeps its
  // place: its row cannot be ticked, and dropping it here would lose the choice for
  // good — Dictionaries::selectedDictionaries() leaves it out of what Chromium is
  // given, and downloading it again brings the tick back.
  for (const QString &code : stored)
    if (!installed.contains(code) || ticked(code))
      chosen << code;
  // Then whatever was ticked since, in the order the list shows them.
  if (m_spellRows)
    for (int i = 0; i < m_spellRows->rowCount(); ++i) {
      const QString code =
          m_spellRows->item(i)->data(DictionaryRows::CodeRole).toString();
      if (m_spellRows->item(i)->checkState() == Qt::Checked &&
          !chosen.contains(code))
        chosen << code;
    }
  SettingsManager::instance().settings().setValue("spellCheckLanguages", chosen);
  updateSpellCheckSummary();
  emit spellCheckChanged();
}

void SettingsWidget::on_themeToggleButtonCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/themeToggleButton", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_zoomButtonsCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("webtweaks/zoomButtons",
                                                  checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_chatListStripButtonCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/chatListStripButton", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_privacyBlurButtonCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue(
      "webtweaks/privacyBlurButton", checked);
  emit webTweaksChanged();
}

void SettingsWidget::on_spellCheckCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("spellCheckEnabled", checked);
  // The list stays open for business with the checker switched off: it is also
  // where dictionaries are downloaded and deleted, and someone who has none would
  // otherwise have no way to get one.
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

void SettingsWidget::populateChatListPreviewSize() {
  ui->chatListPreviewSizeComboBox->blockSignals(true);
  ui->chatListPreviewSizeComboBox->clear();
  const QString current = ChatListStrip::currentPreviewSizeId();
  for (const ChatListStrip::PreviewSize &size : ChatListStrip::previewSizes()) {
    ui->chatListPreviewSizeComboBox->addItem(size.name, size.id);
    if (size.id == current)
      ui->chatListPreviewSizeComboBox->setCurrentIndex(
          ui->chatListPreviewSizeComboBox->count() - 1);
  }
  ui->chatListPreviewSizeComboBox->blockSignals(false);
}

void SettingsWidget::on_chatListPreviewSizeComboBox_currentIndexChanged(
    int index) {
  ChatListStrip::setCurrentPreviewSizeId(
      ui->chatListPreviewSizeComboBox->itemData(index).toString());
  emit chatListStripChanged();
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
    // Named by the same routine as the spell-check languages, so the two lists
    // that a reader compares — the language Whatly speaks and the languages it
    // checks spelling in — call the same language by the same name. That routine
    // names each language in itself ("português", not "Portuguese"), qualifies it
    // by territory where CLDR's default would otherwise be misleading, and falls
    // back to the bare code for anything Qt does not model.
    ui->languageComboBox->addItem(Dictionaries::languageLabel(code), code);
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
  if (checked) // needs a tray icon — see on_hideTrayIconCheckBox_toggled
    ui->hideTrayIconCheckBox->setChecked(false);
}

void SettingsWidget::on_minimizeOnlyFocusedWindowCheckBox_toggled(bool checked) {
  SettingsManager::instance().settings().setValue("minimizeOnlyFocusedWindow",
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
  // Ctrl+W closes this page, exactly as Escape does. Everywhere else in Whatly
  // that key means "put the window away", and reading it as "put the app in the
  // tray" while Settings is the window in front is not what anyone pressing it
  // here means — this is a page you close, and it is the only window that is.
  if (e->key() == Qt::Key_Escape ||
      (e->key() == Qt::Key_W && e->modifiers().testFlag(Qt::ControlModifier))) {
    this->close();
    e->accept();
    return;
  }

  QWidget::keyPressEvent(e);
}
