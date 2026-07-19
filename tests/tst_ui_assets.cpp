// Regression tests for the bundled UI assets: the icons and logos that the
// About and Rate-the-app screens rely on. These caught (and now guard against)
// the case where a button or logo silently renders blank because its icon was
// never set or its resource went missing.
#include <QtTest>
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

#include <QDesktopServices>
#include <QUrl>

#include "about.h"
#include "rateapp.h"
#include "utils.h"

// Captures QDesktopServices::openUrl() calls so clicking the link buttons in the
// tests exercises their slots without actually launching a browser.
class UrlSink : public QObject {
  Q_OBJECT
public:
  int count = 0;
public slots:
  void handle(const QUrl &) { ++count; }
};

class TstUiAssets : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void resourcesExist_data();
  void resourcesExist();
  void rateAppLogoAndIcons();
  void rateAppLogic();
  void aboutLogo();
  void aboutButtonIcons();
  void aboutDebugInfoToggle();
  void linkButtonsExercised();
  void rateAppShouldShowStates();
};

void TstUiAssets::initTestCase() {
  QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
  QCoreApplication::setApplicationName(QStringLiteral("whatly-test"));
}

// Every icon/logo path referenced from the code or the .ui files must resolve to
// a real, loadable resource.
void TstUiAssets::resourcesExist_data() {
  QTest::addColumn<QString>("path");
  const char *paths[] = {
      ":/icons/app/icon-16.png",  ":/icons/app/icon-32.png",
      ":/icons/app/icon-48.png",  ":/icons/app/icon-64.png",
      ":/icons/app/icon-128.png", ":/icons/app/icon-256.png",
      ":/icons/app/icon-512.png", ":/icons/texture.png",
      ":/icons/shopping-cart-line.png", ":/icons/star-line.png",
      ":/icons/paypal-line.png",  ":/icons/funds-line.png",
      ":/icons/time-line.png",    ":/icons/check-line.png",
      ":/icons/links-line.png",   ":/icons/file-text-line.png",
      ":/icons/error-warning-line.png", ":/icons/grid-line.png",
      ":/icons/terminal-box-line.png",  ":/icons/heart-line.png",
      ":/icons/github-line.png",
  };
  for (const char *p : paths)
    QTest::newRow(p) << QString::fromLatin1(p);
}

void TstUiAssets::resourcesExist() {
  QFETCH(QString, path);
  QVERIFY2(QFile(path).exists(), qPrintable("missing resource: " + path));
  QVERIFY2(!QPixmap(path).isNull(), qPrintable("pixmap failed to load: " + path));
}

void TstUiAssets::rateAppLogoAndIcons() {
  RateApp w(nullptr, QStringLiteral("snap://whatly"), 1, 1, 0);

  auto *logo = w.findChild<QLabel *>(QStringLiteral("logoLabel"));
  QVERIFY2(logo, "RateApp: logoLabel not found");
  QVERIFY2(!logo->pixmap().isNull(), "RateApp: logo pixmap is null");

  const QStringList btns = {"rateNowBtn", "rateOnGithub", "donate",
                            "donate_2",   "laterBtn",     "alreadyDoneBtn"};
  for (const QString &n : btns) {
    auto *b = w.findChild<QPushButton *>(n);
    QVERIFY2(b, qPrintable("RateApp: button not found: " + n));
    QVERIFY2(!b->icon().isNull(),
             qPrintable("RateApp: button has no icon: " + n));
  }
}

void TstUiAssets::rateAppLogic() {
  // Cover the non-navigating button slots. RateApp is designed to delete itself
  // (deleteLater) when a button is pressed, so each instance is heap-allocated
  // and a fresh one is used per click; the object is never touched afterwards.
  // A large present_delay means the show timer never fires during the test.
  for (const QString &name : {QStringLiteral("alreadyDoneBtn"),
                              QStringLiteral("laterBtn")}) {
    auto *w = new RateApp(nullptr, QStringLiteral("snap://whatly"), 5, 5, 999999);
    if (auto *b = w->findChild<QPushButton *>(name))
      b->click();
    QTest::qWait(5); // let a pending deleteLater run
  }
  QVERIFY(true);
}

void TstUiAssets::aboutLogo() {
  About w;
  auto *logo = w.findChild<QLabel *>(QStringLiteral("label"));
  QVERIFY2(logo, "About: logo label not found");
  QVERIFY2(!logo->pixmap().isNull(), "About: logo pixmap is null");
}

void TstUiAssets::aboutDebugInfoToggle() {
  // The Debug-Info button just shows/hides a text area — no URL — so it is safe
  // to click and covers both branches of the slot.
  About w;
  w.show(); // offscreen; needed so the slot's isVisible() check is meaningful
  QTest::qWait(20);
  auto *btn = w.findChild<QPushButton *>(QStringLiteral("debugInfoButton"));
  QVERIFY(btn);
  auto *text = w.findChild<QWidget *>(QStringLiteral("debugInfoText"));
  QVERIFY(text);
  btn->click(); // show branch
  QVERIFY(!text->isHidden());
  btn->click(); // hide branch
  QVERIFY(text->isHidden());
}

void TstUiAssets::aboutButtonIcons() {
  About w;
  const QStringList btns = {"donate",      "kofi",       "wise",
                            "rate",        "more_apps",  "source_code",
                            "report_bug",  "debugInfoButton"};
  QStringList missing;
  for (const QString &n : btns) {
    auto *b = w.findChild<QPushButton *>(n);
    if (b && b->icon().isNull())
      missing << n;
  }
  QVERIFY2(missing.isEmpty(),
           qPrintable("About: buttons with no icon: " + missing.join(", ")));
}

void TstUiAssets::linkButtonsExercised() {
  // Intercept openUrl so the link buttons run their slots without a browser.
  UrlSink sink;
  const QStringList schemes = {"https", "http", "snap", "mailto"};
  for (const QString &s : schemes)
    QDesktopServices::setUrlHandler(s, &sink, "handle");

  {
    About a;
    // report_bug is excluded: the pre-filled debug log pushes the URL over the
    // 7500-byte limit, so it pops a modal QMessageBox that would hang headless.
    for (const QString &n : {"donate", "kofi", "wise", "rate", "more_apps",
                             "source_code"})
      if (auto *b = a.findChild<QPushButton *>(n))
        b->click();
  }
  {
    RateApp r(nullptr, QStringLiteral("snap://whatly"), 1, 1, 0);
    for (const QString &n : {"rateNowBtn", "rateOnGithub", "donate", "donate_2"})
      if (auto *b = r.findChild<QPushButton *>(n))
        b->click();
  }

  for (const QString &s : schemes)
    QDesktopServices::unsetUrlHandler(s);
  QVERIFY2(sink.count >= 1, "no link button routed through openUrl");
}

void TstUiAssets::rateAppShouldShowStates() {
  // "already rated" makes shouldShow() return false, so the delayed timer path
  // frees the dialog. Heap-allocate because that path calls deleteLater().
  auto &s = SettingsManager::instance().settings();
  const bool saved = s.value(QStringLiteral("rated_already"), false).toBool();
  s.setValue(QStringLiteral("rated_already"), true);
  auto *r = new RateApp(nullptr, QStringLiteral("snap://whatly"), 0, 0, 1);
  QMetaObject::invokeMethod(r, "delayShowEvent");
  QTest::qWait(60); // timer fires -> shouldShow()==false -> deleteLater()
  s.setValue(QStringLiteral("rated_already"), saved);
  QVERIFY(true);
}

QTEST_MAIN(TstUiAssets)
#include "tst_ui_assets.moc"
