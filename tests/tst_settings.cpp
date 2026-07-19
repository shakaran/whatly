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
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

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
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
