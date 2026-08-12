// In-process coverage of SettingsWidget: construct it standalone and change the
// value of every data control (checkbox, combo, spin box, slider). Changing a
// value fires the same on_*_toggled / _valueChanged / _currentIndexChanged slot
// a real click would, but without pressing the action buttons that open file
// choosers or the permission/automatic-theme dialogs. A safety net rejects any
// dialog that still appears so nothing can block the run.
#include <QtTest>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QRegularExpression>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include "customtitlebar.h"
#include "dictionaries.h"
#include "quickcompose.h"
#include "settingsmanager.h"
#include "settingswidget.h"

class TstSettings : public QObject {
  Q_OBJECT
  QTimer m_modalCloser;
private slots:
  void initTestCase() {
    // Performance:: and NetworkProxy:: keep a machine-wide store that ignores
    // the application name, so setting it below is not enough to keep this test
    // out of the developer's real settings — exerciseEveryControl() drives every
    // spin box to its maximum and every combo through every value. ctest sets
    // this too; doing it here as well covers running the binary directly.
    qputenv("WHATLY_SETTINGS_APP", "whatly-test");
    QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
    QCoreApplication::setApplicationName(QStringLiteral("whatly-test"));
    // As tst_logic already does: redirects the per-account settings too, on the
    // platforms where QSettings is a file (it cannot redirect the Windows
    // registry, which is what the variable above is for).
    QStandardPaths::setTestModeEnabled(true);
    m_modalCloser.setInterval(25);
    connect(&m_modalCloser, &QTimer::timeout, [] {
      for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QDialog *>(w))
          if (d->isVisible())
            d->reject();
    });
    m_modalCloser.start();
  }

  void exerciseEveryControl() {
    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());

    for (auto *cb : sw.findChildren<QCheckBox *>()) {
      cb->setChecked(!cb->isChecked());
      QTest::qWait(1);
      cb->setChecked(!cb->isChecked());
    }
    for (auto *combo : sw.findChildren<QComboBox *>())
      for (int i = 0; i < combo->count(); ++i)
        combo->setCurrentIndex(i);
    for (auto *sp : sw.findChildren<QSpinBox *>()) {
      sp->setValue(sp->minimum());
      sp->setValue(sp->maximum());
    }
    for (auto *sp : sw.findChildren<QDoubleSpinBox *>()) {
      sp->setValue(sp->minimum());
      sp->setValue(sp->maximum());
    }
    for (auto *sl : sw.findChildren<QSlider *>()) {
      sl->setValue(sl->minimum());
      sl->setValue(sl->maximum());
    }
    // Click the action buttons too: the file choosers, the permission dialog and
    // the automatic-theme setup all pop a dialog, which the modal closer above
    // cancels — so the button slot runs up to (and through) the cancel path
    // without blocking. QPointer guards against a slot that deletes widgets.
    QList<QPointer<QPushButton>> buttons;
    for (auto *b : sw.findChildren<QPushButton *>())
      buttons << b;
    for (const QPointer<QPushButton> &b : buttons) {
      if (b) {
        b->click();
        QTest::qWait(5);
      }
    }
    QTest::qWait(20); // let any queued slot work run
    QVERIFY(true);
  }

  // The collapsible sections are named for a Qt style-sheet selector
  // (QGroupBox#name), whose parser rejects non-ASCII — so the name must never be
  // derived from the translated title ("Básico", "IA/traducción"). Enforce the
  // index form, which is locale-independent by construction.
  void sectionNamesAreAsciiIdentifiers() {
    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());
    const QRegularExpression re(QStringLiteral("^whatlySection\\d+$"));
    int seen = 0;
    for (auto *g : sw.findChildren<QGroupBox *>()) {
      const QString n = g->objectName();
      if (!n.startsWith(QStringLiteral("whatlySection")))
        continue;
      ++seen;
      QVERIFY2(re.match(n).hasMatch(),
               qPrintable(QStringLiteral("section name not an ASCII index: ") + n));
    }
    QVERIFY(seen > 0); // the sections exist, so the check actually ran
  }

  // #13: "Hide tray icon" and the two settings that need a tray icon to work
  // (start minimised into the tray, minimise on tray-click) are mutually
  // exclusive, so the window can never be sent somewhere with no icon left to
  // bring it back.
  void traySettingsMutuallyExclusive() {
    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());
    auto *hide = sw.findChild<QCheckBox *>("hideTrayIconCheckBox");
    auto *startMin = sw.findChild<QCheckBox *>("startMinimized");
    auto *minClick = sw.findChild<QCheckBox *>("minimizeOnTrayIconClick");
    QVERIFY(hide && startMin && minClick);

    // Turning on "hide tray icon" turns the other two off.
    startMin->setChecked(true);
    minClick->setChecked(true);
    hide->setChecked(true);
    QVERIFY(!startMin->isChecked());
    QVERIFY(!minClick->isChecked());

    // Turning either of those back on turns "hide tray icon" off again — and the
    // chain stops there (only enabling excludes), so it can't loop.
    startMin->setChecked(true);
    QVERIFY(!hide->isChecked());
    hide->setChecked(true);
    QVERIFY(!startMin->isChecked());
    minClick->setChecked(true);
    QVERIFY(!hide->isChecked());
  }

  // Gert's request #6: the account tabs can move into the title bar, but only
  // where there is a custom title bar for them to move into. A stored "yes"
  // left over from before the custom frame was switched off must not produce a
  // window with neither a native title bar nor buttons of its own.
  void tabsInTitleBarNeedsCustomFrame() {
    const bool frame = CustomTitleBar::isEnabled();

    CustomTitleBar::setTabsInTitleBar(true);
    CustomTitleBar::setEnabled(false);
    QVERIFY(!CustomTitleBar::tabsInTitleBar());

    // Turning the frame back on restores the choice rather than losing it.
    CustomTitleBar::setEnabled(true);
    QVERIFY(CustomTitleBar::tabsInTitleBar());

    CustomTitleBar::setTabsInTitleBar(false);
    QVERIFY(!CustomTitleBar::tabsInTitleBar());

    CustomTitleBar::setEnabled(frame);
  }

  // ...so ticking "Hide the title bar" has to bring the custom frame with it.
  // Greying the box out until the user finds the other one reads as the app
  // refusing a setting for no stated reason, so it is granted instead.
  void hidingTitleBarSwitchesTheFrameOn() {
    const bool frame = CustomTitleBar::isEnabled();
    CustomTitleBar::setEnabled(false);
    CustomTitleBar::setTabsInTitleBar(false);

    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());
    auto *frameBox = sw.findChild<QCheckBox *>("customWindowFrameCheckBox");
    auto *tabsBox = sw.findChild<QCheckBox *>("tabsInTitleBarCheckBox");
    QVERIFY(frameBox && tabsBox);
    QVERIFY(tabsBox->isEnabled()); // never denied
    QVERIFY(!frameBox->isChecked());

    tabsBox->setChecked(true);
    QVERIFY(CustomTitleBar::isEnabled());
    QVERIFY(CustomTitleBar::tabsInTitleBar());
    QVERIFY(frameBox->isChecked()); // and the other box says so

    // Dropping the frame takes the title bar back, and the box stops claiming
    // otherwise — but the stored choice survives for when it is switched on.
    frameBox->setChecked(false);
    QVERIFY(!CustomTitleBar::tabsInTitleBar());
    QVERIFY(!tabsBox->isChecked());
    frameBox->setChecked(true);
    QVERIFY(CustomTitleBar::tabsInTitleBar());

    CustomTitleBar::setEnabled(frame);
    CustomTitleBar::setTabsInTitleBar(false);
  }

  // #9: the settings page is a set of collapsible accordion sections — an arrow
  // header (▾ open / ▸ collapsed) per group that shows/hides the whole group.
  // Only the first is open on launch, and toggling a header reveals/hides its
  // group box.
  void collapsibleSections() {
    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());

    QList<QToolButton *> headers;
    for (auto *tb : sw.findChildren<QToolButton *>())
      if (tb->arrowType() == Qt::DownArrow || tb->arrowType() == Qt::RightArrow)
        headers << tb;
    // The new sub-sections plus the pre-existing groups.
    QVERIFY(headers.size() >= 7);

    // Exactly one section is open on launch.
    int open = 0;
    for (auto *h : headers)
      if (h->isChecked())
        ++open;
    QCOMPARE(open, 1);

    // A collapsed header's group box is hidden; toggling the header reveals it
    // and toggling again hides it (the arrow tracks the state).
    QToolButton *collapsed = nullptr;
    for (auto *h : headers)
      if (!h->isChecked()) {
        collapsed = h;
        break;
      }
    QVERIFY(collapsed);
    QWidget *section = collapsed->parentWidget();
    QVERIFY(section);
    QGroupBox *box = nullptr;
    for (auto *b : section->findChildren<QGroupBox *>())
      if (b->parentWidget() == section) {
        box = b;
        break;
      }
    QVERIFY(box);
    QVERIFY(!box->isVisibleTo(section)); // collapsed to just its header

    collapsed->setChecked(true);
    QVERIFY(box->isVisibleTo(section));
    QCOMPARE(collapsed->arrowType(), Qt::DownArrow);

    collapsed->setChecked(false);
    QVERIFY(!box->isVisibleTo(section));
    QCOMPARE(collapsed->arrowType(), Qt::RightArrow);
  }

  // Quick-compose overlay (#4): emits submitted() with trimmed values only when
  // both fields have content, and stays put (no signal) otherwise.
  void quickComposeSubmit() {
    QuickCompose qc;
    auto *recipient = qc.findChildren<QLineEdit *>().value(0);
    auto *message = qc.findChildren<QLineEdit *>().value(1);
    QVERIFY(recipient && message);

    QSignalSpy spy(&qc, &QuickCompose::submitted);

    // Missing message: no send.
    recipient->setText(QStringLiteral("  Alice  "));
    message->clear();
    QMetaObject::invokeMethod(&qc, "trySend");
    QCOMPARE(spy.count(), 0);

    // Both present: one send, with trimmed values.
    message->setText(QStringLiteral("  hola  "));
    QMetaObject::invokeMethod(&qc, "trySend");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Alice"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("hola"));
  }

  // The wheel in Settings: the page scrolls when the pointer is on the page, the
  // thing under the pointer scrolls when it has content of its own, and whichever
  // of the two the gesture started on keeps the rest of it.
  void wheelStaysWithWhatItStartedOn() {
    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());

    auto *page = sw.findChild<QScrollArea *>(QStringLiteral("scrollArea"));
    auto *list = sw.findChild<QListWidget *>(QStringLiteral("jsAddonsList"));
    auto *combo =
        sw.findChild<QComboBox *>(QStringLiteral("spellCheckLanguageComboBox"));
    auto *control = sw.findChildren<QSpinBox *>().value(0);
    QVERIFY(page && list && combo && combo->view() && control);

    // Give both scrollbars somewhere to go, since this window is never shown and
    // so was never laid out against real content.
    QScrollBar *pageBar = page->verticalScrollBar();
    pageBar->setRange(0, 1000);
    pageBar->setSingleStep(10);
    QScrollBar *listBar = list->verticalScrollBar();
    listBar->setRange(0, 1000);

    auto turn = [](QWidget *at, int notches) {
      QWheelEvent ev(QPointF(2, 2), at->mapToGlobal(QPointF(2, 2)), QPoint(),
                     QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier,
                     Qt::NoScrollPhase, false);
      QApplication::sendEvent(at, &ev);
    };
    // Nothing separates one turn of a mouse wheel from the next but a pause.
    auto letGo = [] { QTest::qWait(500); };

    // On a spin box: the page moves, and the box keeps its value — a wheel must
    // not change a setting the pointer merely passed over.
    const int before = control->value();
    pageBar->setValue(100);
    turn(control, -1);
    QVERIFY(pageBar->value() > 100);
    QCOMPARE(control->value(), before);

    // On the open language list: the page stays exactly where it was. It is a
    // window of its own, so scrolling the page leaves it floating in mid-air over
    // settings it has nothing to do with.
    letGo();
    pageBar->setValue(100);
    listBar->setValue(500);
    turn(combo->view()->viewport(), -1);
    QCOMPARE(pageBar->value(), 100);

    // On a list inside the page that has more to show: also the list's own.
    letGo();
    turn(list->viewport(), -1);
    QCOMPARE(pageBar->value(), 100);

    // But a page scroll that crosses that same list carries on down the page,
    // instead of dying the moment the pointer touches it.
    letGo();
    turn(control, -1);
    const int crossing = pageBar->value();
    turn(list->viewport(), -1);
    QVERIFY(pageBar->value() > crossing);
  }

  // Dogfood round 18, two findings against the spell-check picker.
  //
  // One: the language box shows a summary of what is ticked, but which language is
  // being checked can be changed from the tray menu or the keyboard — and an open
  // Settings page went on showing "3 languages" while one of the three was doing
  // the work, because it only re-read that line when the picker itself was used.
  //
  // Two: the box is editable so it can show that summary, which is exactly why a
  // click on it did nothing — an editable combo opens its list from the arrow alone.
  void languageBoxFollowsTheFocusAndOpensOnClick() {
    QTemporaryDir dicts;
    QVERIFY(dicts.isValid());
    for (const QString &n : {"eo", "en_US", "es_ES"}) {
      QFile f(dicts.filePath(n + QStringLiteral(".bdic")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("BDIC-stub");
      f.close();
    }
    // Set before constructing: the picker is filled during construction.
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dicts.path().toLocal8Bit());
    auto &s = SettingsManager::instance().settings();
    s.setValue(QStringLiteral("spellCheckEnabled"), true);
    s.setValue(QStringLiteral("spellCheckLanguages"),
               QStringList{QStringLiteral("en_US"), QStringLiteral("eo"),
                           QStringLiteral("es_ES")});
    s.remove(QStringLiteral("spellCheckFocus"));

    QTemporaryDir cache, storage;
    SettingsWidget sw(nullptr, 0, cache.path(), storage.path());
    auto *combo =
        sw.findChild<QComboBox *>(QStringLiteral("spellCheckLanguageComboBox"));
    QVERIFY(combo && combo->lineEdit());

    // Three ticked, none focused: the count.
    QCOMPARE(combo->lineEdit()->text(), QStringLiteral("3 languages"));

    // Every English dictionary used to read "American English": the label was built
    // from the language alone, and Qt fills the dropped territory back in with the
    // likeliest one. Two English variants must not share a name.
    const QString us = Dictionaries::languageLabel(QStringLiteral("en_US"));
    const QString gb = Dictionaries::languageLabel(QStringLiteral("en_GB"));
    QVERIFY(us != gb);
    QVERIFY(gb.startsWith(QStringLiteral("British")));
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("en_AU"))
                .startsWith(QStringLiteral("Australian")));
    // The other half of it: CLDR gives its *default* variant the bare name, so
    // "português" is the Brazilian one and "español" the Argentinian one, and
    // neither said so. The territory has to be named for those.
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("pt_BR"))
                .contains(QStringLiteral("Brasil")));
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("es_AR"))
                .contains(QStringLiteral("Argentina")));
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("pt_BR")) !=
            Dictionaries::languageLabel(QStringLiteral("pt_PT")));
    // ...and exactly once: "español de España" has its territory taken out of the
    // name before it goes back in as the qualifier, so the whole list keeps one
    // shape and the Spanish entries read alike.
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("es_ES")),
             QStringLiteral("español (España)"));
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("es_MX")),
             QStringLiteral("español (México)"));
    // The bundle ships both of these and Qt reads them as one locale, so the label
    // has to keep them apart.
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("de_DE")) !=
            Dictionaries::languageLabel(QStringLiteral("de_DE_neu")));
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("eo")),
             QStringLiteral("Esperanto (eo)"));
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("zz_ZZ")),
             QStringLiteral("zz_ZZ"));

    // What the tray menu and Ctrl+Alt+S do, followed by the call MainWindow makes.
    Dictionaries::setFocusedDictionary(QStringLiteral("eo"));
    sw.updateSpellCheckSummary();
    QCOMPARE(combo->lineEdit()->text(), QStringLiteral("eo of 3 chosen"));

    // ...and back again, so the line is not one-way.
    Dictionaries::setFocusedDictionary(QString());
    sw.updateSpellCheckSummary();
    QCOMPARE(combo->lineEdit()->text(), QStringLiteral("3 languages"));

    // A click anywhere on the box opens the list — deferred to the next turn of
    // the event loop, so the release that follows the press cannot be mistaken for
    // a click outside a list that has only just appeared (which made it flash and
    // vanish). Closing again is Qt's own doing: with the list open it holds the
    // mouse, and a synthesised press cannot reproduce that grab, so this checks
    // only the half that is ours.
    QVERIFY(combo->view());
    QVERIFY(!combo->view()->isVisible());
    QTest::mousePress(combo->lineEdit(), Qt::LeftButton);
    QTest::mouseRelease(combo->lineEdit(), Qt::LeftButton);
    QVERIFY2(combo->view()->isVisible(),
             "a click on the box must open the list and it must survive the "
             "release that follows");
    combo->hidePopup();
    // What this cannot check: the list flashing up and vanishing again, which is
    // what the click used to do. That needs the mouse grab a shown popup takes, and
    // a synthesised release goes to the line edit rather than to the list, so it
    // stays open here either way. The fix for it — handing the click to the arrow so
    // Qt's own path swallows the release — is verified by hand; this pins the part
    // that can be: that clicking the box opens the list at all.

    s.remove(QStringLiteral("spellCheckLanguages"));
    s.remove(QStringLiteral("spellCheckFocus"));
    qunsetenv("QTWEBENGINE_DICTIONARIES_PATH");
  }
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
