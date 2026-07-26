// In-process coverage of SettingsWidget: construct it standalone and change the
// value of every data control (checkbox, combo, spin box, slider). Changing a
// value fires the same on_*_toggled / _valueChanged / _currentIndexChanged slot
// a real click would, but without pressing the action buttons that open file
// choosers or the permission/automatic-theme dialogs. A safety net rejects any
// dialog that still appears so nothing can block the run.
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include "settingswidget.h"

class TstSettings : public QObject {
  Q_OBJECT
  QTimer m_modalCloser;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
    QCoreApplication::setApplicationName(QStringLiteral("whatly-test"));
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
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
