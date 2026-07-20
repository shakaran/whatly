// Logic-level unit tests: the pure, headless parts of the app — helpers, the
// injected-script generators, the scheduled-message queue, sun calculations,
// identicons, palettes and dictionary resolution. No widgets, no WebEngine
// instances, no event loop, so it runs fast and offscreen.
#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QProcess>
#include <QPixmap>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>

#include "utils.h"
#include "common.h"
#include "debuglog.h"
#include "appprofile.h"
#include "settingsmanager.h"
#include "dictionaries.h"
#include "identicons.h"
#include "theme.h"
#include "scheduledmessages.h"
#include "sunclock.hpp"
#include "webfont.h"
#include "chattheme.h"
#include "mutedstatus.h"
#include "privacyblur.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "customjs.h"
#include "commandpalette.h"
#include "updatechecker.h"
#include "storageinfo.h"
#include "shortcuts.h"
#include "backup.h"
#include "webtweaks.h"
#include "linkeddevicename.h"
#include "performance.h"
#include "networkproxy.h"
#include "notificationrules.h"
#include "autostart.h"
#include "portalnotification.h"
#include "setupwizard.h"
#include "settingsmanager.h"

#include <QNetworkProxy>

// ─────────────────────────────────────────────────────────────────────────────
class TstUtils : public QObject {
  Q_OBJECT
private slots:
  void toCamelCase() {
    QCOMPARE(Utils::toCamelCase(QStringLiteral("hello world")),
             QStringLiteral("Hello World"));
    QCOMPARE(Utils::toCamelCase(QString()), QString());
  }
  void randomIds() {
    const QString a = Utils::generateRandomId(16);
    QCOMPARE(a.length(), 16);
    QVERIFY(a != Utils::generateRandomId(16)); // practically never equal
    QCOMPARE(Utils::genRand(8, true, false, false).length(), 8);
    const QString digits = Utils::genRand(20, false, false, true);
    for (const QChar &c : digits)
      QVERIFY(c.isDigit());
  }
  void secToDay() {
    const QString s = Utils::convertSectoDay(3661); // 1h 1m 1s
    QVERIFY(!s.isEmpty());
  }
  void xmlRoundTrip() {
    const QString raw = QStringLiteral("a<b>&\"'c");
    const QString enc = Utils::encodeXML(raw);
    QVERIFY(!enc.contains('<'));
    QCOMPARE(Utils::decodeXML(enc), raw);
  }
  void roundToOneDecimal() {
    QCOMPARE(Utils::RoundToOneDecimal(1.24f), 1.2f);
    QCOMPARE(Utils::RoundToOneDecimal(1.25f), 1.3f);
  }
  void phoneNumbers() {
    QVERIFY(Utils::isPhoneNumber(QStringLiteral("+34600123456")));
    QVERIFY(!Utils::isPhoneNumber(QStringLiteral("600123456"))); // no +
    QVERIFY(!Utils::isPhoneNumber(QStringLiteral("not a phone")));
    QVERIFY(!Utils::isPhoneNumber(QString()));
  }
  void desktopAndInstall() {
    QVERIFY(!Utils::detectDesktopEnvironment().isEmpty());
    Utils::getInstallType(); // may be empty for an uninstalled/native build
    QVERIFY(!Utils::appDebugInfo().isEmpty());
    const QString md = Utils::appDebugInfoMarkdown();
    QVERIFY(md.contains(QLatin1String("Version")));
    QVERIFY(!Utils::processMemoryInfo().isEmpty());
  }
  // The critical guard from issue #230: a cache delete must refuse any path that
  // could nuke something it shouldn't. None of these should ever delete.
  void deleteCacheRefusesDangerousPaths() {
    QVERIFY(!Utils::delete_cache(QString()));                 // empty
    QVERIFY(!Utils::delete_cache(QStringLiteral(".")));       // relative (cwd)
    QVERIFY(!Utils::delete_cache(QStringLiteral("relative/x")));
    QVERIFY(!Utils::delete_cache(QStringLiteral("/")));       // root
    QVERIFY(!Utils::delete_cache(QDir::homePath()));          // home
  }
  // The happy path: a directory under the app's own cache location is safe to
  // delete, so the guard lets it through and it is actually cleared.
  void deleteCacheAcceptsOwnedPath() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QVERIFY(!base.isEmpty());
    const QString target = base + QStringLiteral("/whatly-test-deletable");
    QVERIFY(QDir().mkpath(target + QStringLiteral("/sub")));
    QFile f(target + QStringLiteral("/sub/x.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    QVERIFY(Utils::delete_cache(target)); // owned → deleted (then re-created)
    QVERIFY(!QFileInfo::exists(target + QStringLiteral("/sub/x.txt")));
    QDir(target).removeRecursively();
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstAppProfile : public QObject {
  Q_OBJECT
private slots:
  void defaults() {
    // Without initFromArgs the default profile is active and adds no suffix.
    QVERIFY(AppProfile::isDefault());
    QVERIFY(AppProfile::suffix().isEmpty());
    AppProfile::id();    // defined (may be empty for the default profile)
    AppProfile::label(); // defined
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstIdenticons : public QObject {
  Q_OBJECT
private slots:
  void letterTileSize() {
    const QPixmap p = Identicons::letterTile(QStringLiteral("Whatly"), QSize(128, 128));
    QVERIFY(!p.isNull());
    QCOMPARE(p.size(), QSize(128, 128));
  }
  void clipRRect() {
    QPixmap in(64, 64);
    in.fill(Qt::red);
    const QPixmap out = Identicons::clipRRect(in);
    QVERIFY(!out.isNull());
  }
  void colorCount() {
    QImage solid(10, 10, QImage::Format_ARGB32);
    solid.fill(Qt::blue);
    QCOMPARE(Identicons::colorCount(solid), quint32(1));
    solid.setPixelColor(0, 0, Qt::red);
    QCOMPARE(Identicons::colorCount(solid), quint32(2));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstTheme : public QObject {
  Q_OBJECT
private slots:
  void palettesDiffer() {
    const QPalette light = Theme::getLightPalette();
    const QPalette dark = Theme::getDarkPalette();
    QVERIFY(light.color(QPalette::Window) != dark.color(QPalette::Window));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstDictionaries : public QObject {
  Q_OBJECT
private slots:
  void apiDoesNotCrash() {
    // These depend on the install layout and the system locale, both of which
    // vary in a test/CI environment — so just exercise them without asserting a
    // particular value (a bare "C" locale legitimately yields no preference).
    Dictionaries::dictionaryPath();
    const QStringList all = Dictionaries::availableDictionaries();
    QVERIFY(all.size() >= 0);
    Dictionaries::preferredDictionary();
    Dictionaries::selectedDictionaries();
  }
  // Point the resolver at a directory of fake .bdic files so the full selection
  // logic (availability, locale preference, stored-list filtering) runs.
  void withFixture() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &n : {"en-US", "es-ES", "fr"}) {
      QFile f(dir.filePath(n + QStringLiteral(".bdic")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("BDIC-stub");
      f.close();
    }
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dir.path().toLocal8Bit());

    QCOMPARE(QDir(Dictionaries::dictionaryPath()).canonicalPath(),
             QDir(dir.path()).canonicalPath());
    const QStringList all = Dictionaries::availableDictionaries();
    QVERIFY(all.contains(QStringLiteral("en-US")));
    QVERIFY(all.contains(QStringLiteral("es-ES")));
    QVERIFY(!Dictionaries::preferredDictionary().isEmpty());

    // Stored selection: keep the installed ones, drop the uninstalled.
    SettingsManager::instance().settings().setValue(
        QStringLiteral("spellCheckLanguages"),
        QStringList{QStringLiteral("es-ES"), QStringLiteral("zz-ZZ")});
    const QStringList sel = Dictionaries::selectedDictionaries();
    QVERIFY(sel.contains(QStringLiteral("es-ES")));
    QVERIFY(!sel.contains(QStringLiteral("zz-ZZ")));

    SettingsManager::instance().settings().remove(
        QStringLiteral("spellCheckLanguages"));
    qunsetenv("QTWEBENGINE_DICTIONARIES_PATH");
  }
  // A dictionary named exactly after the system locale is the preferred one
  // (covers the exact-locale-match branch of preferredDictionary).
  void localeExactMatch() {
    QTemporaryDir dir;
    const QString loc = QLocale::system().name(); // "en_US", or "C" on CI
    for (const QString &n : {loc, QStringLiteral("en_US")}) {
      QFile f(dir.filePath(n + QStringLiteral(".bdic")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
      f.close();
    }
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dir.path().toLocal8Bit());
    QCOMPARE(Dictionaries::preferredDictionary(), loc);
    qunsetenv("QTWEBENGINE_DICTIONARIES_PATH");
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstSunclock : public QObject {
  Q_OBJECT
private slots:
  void sunriseBeforeNoonBeforeSunset() {
    // Madrid, summer. Ordering must hold regardless of the absolute values.
    Sunclock sc(40.4168, -3.7038, 2);
    struct tm t = {};
    t.tm_year = 2021 - 1900;
    t.tm_mon = 5; // June
    t.tm_mday = 21;
    t.tm_hour = 12;
    const time_t date = timegm(&t);
    const time_t rise = sc.sunrise(date);
    const time_t noon = sc.solar_noon(date);
    const time_t set = sc.sunset(date);
    QVERIFY(rise < noon);
    QVERIFY(noon < set);
  }
  void irradianceInRange() {
    Sunclock sc(40.4168, -3.7038, 2);
    struct tm t = {};
    t.tm_year = 2021 - 1900;
    t.tm_mon = 5;
    t.tm_mday = 21;
    t.tm_hour = 12;
    const double v = sc.irradiance(timegm(&t));
    QVERIFY(v >= 0.0 && v <= 1.0);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstScheduled : public QObject {
  Q_OBJECT
private slots:
  void recurrenceNextOccurrence() {
    using R = ScheduledMessages::Recurrence;
    const QDateTime base(QDate(2026, 1, 1), QTime(9, 0)); // Thu 2026-01-01
    QCOMPARE(ScheduledMessages::nextOccurrence(base, R::None), QDateTime());
    QCOMPARE(ScheduledMessages::nextOccurrence(base, R::Daily),
             base.addDays(1));
    QCOMPARE(ScheduledMessages::nextOccurrence(base, R::Weekly),
             base.addDays(7));
    // 2026-01-01 is a Thursday → next weekday is Friday (the 2nd).
    QCOMPARE(ScheduledMessages::nextOccurrence(base, R::Weekdays).date(),
             QDate(2026, 1, 2));
    // From a Friday, weekdays should skip the weekend to Monday.
    const QDateTime fri(QDate(2026, 1, 2), QTime(9, 0));
    QCOMPARE(ScheduledMessages::nextOccurrence(fri, R::Weekdays).date(),
             QDate(2026, 1, 5)); // Monday
  }
  void recurringReschedulesOnSend() {
    ScheduledMessages sm;
    const QDateTime due = QDateTime::currentDateTime().addSecs(-10); // overdue
    const QString id = sm.add(QStringLiteral("34600000000"), QStringLiteral("R"),
                              QStringLiteral("daily ping"), due,
                              ScheduledMessages::Recurrence::Daily);
    sm.reportResult(id, true, QString());
    // Still pending (rescheduled), with a due time in the future.
    const auto entries = sm.entries();
    bool found = false;
    for (const auto &e : entries)
      if (e.id == id) {
        found = true;
        QCOMPARE(e.status, ScheduledMessages::Status::Pending);
        QVERIFY(e.dueAt > QDateTime::currentDateTime());
      }
    QVERIFY(found);
    sm.remove(id);
  }
  void addRemoveAndStatus() {
    ScheduledMessages sm;
    const int before = sm.entries().size();
    const QString id = sm.add(QStringLiteral("34600123456"),
                              QStringLiteral("Alice"),
                              QStringLiteral("hi there"),
                              QDateTime::currentDateTime().addSecs(3600));
    QVERIFY(!id.isEmpty());
    QCOMPARE(sm.entries().size(), before + 1);

    sm.reportResult(id, false, QStringLiteral("boom"));
    bool found = false;
    for (const auto &e : sm.entries())
      if (e.id == id) {
        QCOMPARE(e.status, ScheduledMessages::Status::Failed);
        QCOMPARE(e.error, QStringLiteral("boom"));
        found = true;
      }
    QVERIFY(found);

    sm.remove(id);
    for (const auto &e : sm.entries())
      QVERIFY(e.id != id);
  }
  void removeCompleted() {
    ScheduledMessages sm;
    const QString a = sm.add(QStringLiteral("34600000001"), QString(),
                             QStringLiteral("x"),
                             QDateTime::currentDateTime().addSecs(3600));
    sm.reportResult(a, true, QString()); // Sent
    sm.removeCompleted();
    for (const auto &e : sm.entries())
      QVERIFY(e.id != a);
  }
  void statusLabels() {
    QVERIFY(!ScheduledMessages::statusLabel(ScheduledMessages::Status::Pending).isEmpty());
    QVERIFY(!ScheduledMessages::statusLabel(ScheduledMessages::Status::Sent).isEmpty());
    QVERIFY(!ScheduledMessages::statusLabel(ScheduledMessages::Status::Failed).isEmpty());
  }
  void scripts() {
    QVERIFY(!ScheduledMessages::senderScriptSource().isEmpty());
    const QString job = ScheduledMessages::startJobScript(
        QStringLiteral("id1"), QStringLiteral("34600123456"),
        QStringLiteral("hello"));
    QVERIFY(job.contains(QLatin1String("34600123456")));
  }
  // A message already past its due time must fire sendRequested once start()
  // gates sending on — this exercises start()/checkDue() and the due scan.
  void firesOverdueMessage() {
    ScheduledMessages sm;
    QSignalSpy spy(&sm, &ScheduledMessages::sendRequested);
    const QString id = sm.add(QStringLiteral("34600555555"),
                              QStringLiteral("Past"), QStringLiteral("overdue"),
                              QDateTime::currentDateTime().addSecs(-60));
    sm.start(); // gates sending on, then checkDue() runs immediately
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy.first().at(0).toString(), id);
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("34600555555"));

    // Reporting success clears the in-flight marker; a second scan finds nothing.
    sm.reportResult(id, true, QString());
    const int fired = spy.count();
    sm.start(); // calls checkDue() again
    QCOMPARE(spy.count(), fired); // no new send
    sm.removeCompleted();
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The injected-script modules: scriptSource() must be non-trivial, and the
// settings-backed selection must round-trip.
class TstScripts : public QObject {
  Q_OBJECT
private slots:
  void webFont() {
    QVERIFY(!WebFont::families().isEmpty());
    WebFont::setCurrentFamily(QStringLiteral("DejaVu Sans"));
    QCOMPARE(WebFont::currentFamily(), QStringLiteral("DejaVu Sans"));
    QVERIFY(!WebFont::scriptSource().isEmpty());
    WebFont::setCurrentFamily(QString()); // back to WhatsApp default
    QVERIFY(WebFont::currentFamily().isEmpty());
    WebFont::scriptSource(); // empty-family branch (jsCssFor "")
  }
  void chatTheme() {
    const auto themes = ChatTheme::themes();
    QVERIFY(!themes.isEmpty());
    // setter/getter round-trip
    ChatTheme::setCurrentThemeId(themes.last().id);
    QCOMPARE(ChatTheme::currentThemeId(), themes.last().id);
    // The default theme is a no-op (empty script), but at least one real theme
    // must produce a stylesheet.
    bool anyScript = false;
    for (const auto &th : themes) {
      ChatTheme::setCurrentThemeId(th.id);
      if (!ChatTheme::scriptSource().isEmpty())
        anyScript = true;
    }
    QVERIFY(anyScript);
  }
  void mutedStatus() {
    MutedStatus::setEnabled(true);
    QVERIFY(MutedStatus::isEnabled());
    QVERIFY(!MutedStatus::scriptSource().isEmpty());
    MutedStatus::setEnabled(false);
    QVERIFY(!MutedStatus::isEnabled());
    MutedStatus::scriptSource(); // disabled branch
  }
  void privacyBlur() {
    const auto levels = PrivacyBlur::levels();
    QVERIFY(!levels.isEmpty());
    // The first level is usually "off" (empty script); a later one recolours.
    PrivacyBlur::setCurrentLevelId(levels.first().id);
    QCOMPARE(PrivacyBlur::currentLevelId(), levels.first().id);
    PrivacyBlur::scriptSource(); // off/default branch
    bool anyScript = false;
    for (const auto &lv : levels) {
      PrivacyBlur::setCurrentLevelId(lv.id);
      if (!PrivacyBlur::scriptSource().isEmpty())
        anyScript = true;
    }
    QVERIFY(anyScript);
  }
  void otherScriptsNonEmpty() {
    QVERIFY(!ChatWallpaper::scriptSource().isEmpty());
    QVERIFY(!WebTweaks::scriptSource().isEmpty());
    QVERIFY(!LinkedDeviceName::scriptSource(QStringLiteral("Work")).isEmpty());
    // CustomCss without a file is inactive but the script must still be valid.
    CustomCss::scriptSource();
    QVERIFY(!CustomCss::isActive());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstUtilsMore : public QObject {
  Q_OBJECT
private slots:
  void envVar() {
    qputenv("WHATLY_TEST_ENV", "hello");
    QCOMPARE(Utils::GetEnvironmentVar(QStringLiteral("WHATLY_TEST_ENV")),
             QStringLiteral("hello"));
  }
  void returnPath() {
    const QString p = Utils::returnPath(QStringLiteral("sub"),
                                        QDir::tempPath());
    QVERIFY(!p.isEmpty());
  }
  void refreshCacheSize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A nested file so dir_size() recurses into a subdirectory.
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
    QFile f(dir.filePath(QStringLiteral("a.bin")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(2048, 'x'));
    f.close();
    QFile g(dir.filePath(QStringLiteral("sub/b.bin")));
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write(QByteArray(4096, 'y'));
    g.close();
    const QString s = Utils::refreshCacheSize(dir.path());
    QVERIFY(!s.isEmpty());
  }
  void camelCaseVariants() {
    QCOMPARE(Utils::toCamelCase(QStringLiteral("one two three")),
             QStringLiteral("One Two Three"));
  }
  void installTypeFromEnv() {
    qputenv("INSTALL_TYPE", "snap");
    QCOMPARE(Utils::getInstallType(), QStringLiteral("snap"));
    qunsetenv("INSTALL_TYPE");
    // Flatpak is inferred from FLATPAK_ID when INSTALL_TYPE is unset.
    qputenv("FLATPAK_ID", "net.shakaran.whatly");
    QCOMPARE(Utils::getInstallType(), QStringLiteral("flatpak"));
    qunsetenv("FLATPAK_ID");
  }
  void desktopEnvBranches() {
    const QByteArray savedXdg = qgetenv("XDG_CURRENT_DESKTOP");
    const QByteArray savedWm = qgetenv("WINDOWMANAGER");
    const QByteArray savedSession = qgetenv("DESKTOP_SESSION");

    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    QCOMPARE(Utils::detectDesktopEnvironment(), QStringLiteral("KDE"));

    qunsetenv("XDG_CURRENT_DESKTOP");
    qputenv("WINDOWMANAGER", "i3");
    qunsetenv("DESKTOP_SESSION");
    QCOMPARE(Utils::detectDesktopEnvironment(), QStringLiteral("i3"));

    qunsetenv("WINDOWMANAGER");
    qputenv("DESKTOP_SESSION", "plasma");
    QCOMPARE(Utils::detectDesktopEnvironment(), QStringLiteral("plasma"));

    qunsetenv("DESKTOP_SESSION");
    QCOMPARE(Utils::detectDesktopEnvironment(),
             QStringLiteral("Unknown Desktop Environment"));

    // Restore, so later tests see the real environment.
    if (!savedXdg.isEmpty()) qputenv("XDG_CURRENT_DESKTOP", savedXdg);
    if (!savedWm.isEmpty()) qputenv("WINDOWMANAGER", savedWm);
    if (!savedSession.isEmpty()) qputenv("DESKTOP_SESSION", savedSession);
  }
  void secToDayComponents() {
    const QString s = Utils::convertSectoDay(90061); // 1d 1h 1m 1s
    QVERIFY(s.contains(QLatin1String("1 days")));
    QVERIFY(s.contains(QLatin1String("1 hours")));
  }
  void encodeAllSpecials() {
    const QString enc = Utils::encodeXML(QStringLiteral("<&>\"'"));
    QVERIFY(!enc.contains('<'));
    QVERIFY(!enc.contains('>'));
    QCOMPARE(Utils::decodeXML(enc), QStringLiteral("<&>\"'"));
  }
  void genRandCharsets() {
    const QString up = Utils::genRand(30, true, false, false);
    for (const QChar &c : up) QVERIFY(c.isUpper() || !c.isLetter());
    const QString lo = Utils::genRand(30, false, true, false);
    for (const QChar &c : lo) QVERIFY(c.isLower() || !c.isLetter());
  }
  void desktopOpenUrlDoesNotThrow() {
    // Exercises the xdg-open path; waiting lets the async finished handler (and
    // its QDesktopServices fallback) run for a file that cannot be opened.
    Utils::desktopOpenUrl(QStringLiteral("/tmp/whatly-nonexistent-test.txt"));
    QTest::qWait(600);
    QVERIFY(true);
  }
  void appDebugInfoContents() {
    const QString info = Utils::appDebugInfo();
    QVERIFY(info.contains(QLatin1String("test"))); // VERSIONSTR="test"
    const QString md = Utils::appDebugInfoMarkdown();
    QVERIFY(md.contains(QLatin1String("Commit")));
    // With an install type set, the markdown includes that row too.
    qputenv("INSTALL_TYPE", "snap");
    QVERIFY(Utils::appDebugInfoMarkdown().contains(QLatin1String("snap")));
    qunsetenv("INSTALL_TYPE");
  }
  // With a live child process, processMemoryInfo() walks the /proc tree and sums
  // the descendant's RSS — covering the tree-walk that has no children otherwise.
  void processMemoryWalksChildren() {
    QProcess child;
    child.start(QStringLiteral("sleep"), {QStringLiteral("5")});
    if (!child.waitForStarted(1500))
      QSKIP("sleep not available on this platform");
    QTest::qWait(150); // let the child's /proc entry become readable
    const QString info = Utils::processMemoryInfo();
    QVERIFY(!info.isEmpty());
    child.terminate();
    child.waitForFinished(2000);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstCommon : public QObject {
  Q_OBJECT
private slots:
  void themeIconFallback() {
    const QIcon icon = themeIcon(QStringLiteral("whatly"),
                                 QStringLiteral(":/icons/app/icon-64.png"));
    QVERIFY(!icon.isNull());
    QVERIFY(!whatsAppOrigin.isEmpty());
    QVERIFY(!defaultUserAgentStr.isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstDebugLog : public QObject {
  Q_OBJECT
private slots:
  void captureAndFilter() {
    DebugLog::install(); // idempotent
    DebugLog::install(); // exercise the already-installed early return
    qWarning("whatly-test-marker-visible");
    qInfo("whatly-test-info-line");        // QtInfoMsg branch
    qCritical("whatly-test-critical-line"); // QtCriticalMsg branch
    // Benign teardown noise is still captured in the log, just not printed.
    qWarning("QThreadStorage: entry 7 destroyed before end of thread");
    const QString recent = DebugLog::recent(50);
    QVERIFY(recent.contains(QLatin1String("whatly-test-marker-visible")));
    QVERIFY(recent.contains(QLatin1String("whatly-test-info-line")));
    QVERIFY(recent.contains(QLatin1String("QThreadStorage: entry 7")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstAppProfileArgs : public QObject {
  Q_OBJECT
private slots:
  void namedProfileThenDefault() {
    // A named profile adds a suffix and is not the default.
    char arg0[] = "whatly";
    char arg1[] = "--profile=work";
    char *argv[] = {arg0, arg1, nullptr};
    // initFromArgs settles the profile once, for the lifetime of the process
    // (as in the real app), so this runs last and does not reset afterwards.
    AppProfile::initFromArgs(2, argv);
    QVERIFY(!AppProfile::isDefault());
    QVERIFY(!AppProfile::suffix().isEmpty());
    QVERIFY(AppProfile::id().contains(QLatin1String("work")));

    // The space-separated "-p <name>" form takes a different parse branch.
    char pShort[] = "-p";
    char pName[] = "team";
    char *argvShort[] = {arg0, pShort, pName, nullptr};
    AppProfile::initFromArgs(3, argvShort);
    QVERIFY(AppProfile::id().contains(QLatin1String("team")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstCustomCss : public QObject {
  Q_OBJECT
private slots:
  void loadAndClear() {
    QTemporaryFile css;
    css.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_XXXXXX.css"));
    QVERIFY(css.open());
    css.write("body { background: #123456; }\n");
    css.close();

    QString err;
    QVERIFY2(CustomCss::setFromFile(css.fileName(), &err), qPrintable(err));
    QVERIFY(CustomCss::isActive());
    QVERIFY(CustomCss::css().contains(QLatin1String("#123456")));
    QVERIFY(!CustomCss::scriptSource().isEmpty());

    CustomCss::clear();
    QVERIFY(!CustomCss::isActive());
  }
  void rejectsMissingFile() {
    QString err;
    QVERIFY(!CustomCss::setFromFile(QStringLiteral("/no/such.css"), &err));
    QVERIFY(!err.isEmpty());
  }

  // The per-account CSS path uses the profile suffix; the default account keeps
  // the historical filename so nothing moves on upgrade.
  void pathHasNoSuffixForDefaultAccount() {
    QTemporaryFile css;
    css.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_XXXXXX.css"));
    QVERIFY(css.open());
    css.write("body{}\n");
    css.close();
    QString err;
    QVERIFY(CustomCss::setFromFile(css.fileName(), &err));
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Default (no --profile) => "custom.css", not "custom-<name>.css".
    QVERIFY(QFile::exists(appData + QStringLiteral("/custom.css")));
    CustomCss::clear();
  }
  // Make the app data directory unwritable so the write paths of both CustomCss
  // and ChatWallpaper report an error instead of silently failing.
  void reportsWriteFailures() {
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(!appData.isEmpty());
    QVERIFY(QDir().mkpath(appData));
    const QFileDevice::Permissions orig = QFileInfo(appData).permissions();
    QFile::setPermissions(appData, QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                       QFileDevice::ReadUser | QFileDevice::ExeUser);

    const bool stillWritable = QFileInfo(appData).isWritable();

    QTemporaryFile css;
    css.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_XXXXXX.css"));
    QVERIFY(css.open());
    css.write("body{}");
    css.close();
    QString cssErr;
    const bool cssOk = CustomCss::setFromFile(css.fileName(), &cssErr);

    QTemporaryDir imgDir;
    const QString img = imgDir.filePath(QStringLiteral("i.png"));
    QImage(16, 16, QImage::Format_ARGB32).save(img);
    QString wpErr;
    const bool wpOk = ChatWallpaper::setImage(img, &wpErr);

    QFile::setPermissions(appData, orig); // restore before asserting

    if (stillWritable)
      QSKIP("app data dir stayed writable (running as root?) — cannot fault-inject");
    QVERIFY(!cssOk);
    QVERIFY(!cssErr.isEmpty());
    QVERIFY(!wpOk);
    QVERIFY(!wpErr.isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstChatWallpaper : public QObject {
  Q_OBJECT
private slots:
  void setStoreClear() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("wp.png"));
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::darkCyan);
    QVERIFY(img.save(src));

    QString err;
    QVERIFY2(ChatWallpaper::setImage(src, &err), qPrintable(err));
    QVERIFY(!ChatWallpaper::storedImagePath().isEmpty());
    QVERIFY(!ChatWallpaper::scriptSource().isEmpty());

    ChatWallpaper::clear();
    QVERIFY(ChatWallpaper::storedImagePath().isEmpty());
  }
  void scalesLargeImage() {
    QTemporaryDir dir;
    const QString src = dir.filePath(QStringLiteral("big.png"));
    QImage img(3000, 2000, QImage::Format_ARGB32); // over the max edge
    img.fill(Qt::magenta);
    QVERIFY(img.save(src));
    QString err;
    QVERIFY2(ChatWallpaper::setImage(src, &err), qPrintable(err));
    QVERIFY(!ChatWallpaper::storedImagePath().isEmpty());
    ChatWallpaper::clear();
  }
  void rejectsBadImage() {
    QTemporaryFile txt;
    txt.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_XXXXXX.txt"));
    QVERIFY(txt.open());
    txt.write("not an image");
    txt.close();
    QString err;
    QVERIFY(!ChatWallpaper::setImage(txt.fileName(), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!ChatWallpaper::setImage(QStringLiteral("/no/such.png"), &err));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
class TstScheduledPersistence : public QObject {
  Q_OBJECT
private slots:
  void survivesReload() {
    QString id;
    {
      ScheduledMessages sm;
      id = sm.add(QStringLiteral("34600999999"), QStringLiteral("Persist"),
                  QStringLiteral("saved to disk"),
                  QDateTime::currentDateTime().addSecs(7200));
      QVERIFY(!id.isEmpty());
    }
    // A fresh instance loads the queue back from disk.
    ScheduledMessages sm2;
    bool found = false;
    for (const auto &e : sm2.entries())
      if (e.id == id) {
        QCOMPARE(e.name, QStringLiteral("Persist"));
        found = true;
      }
    QVERIFY(found);
    sm2.remove(id); // clean up
  }
  // A Failed status must round-trip through disk (covers load()'s status parse).
  void failedStatusPersists() {
    QString id;
    {
      ScheduledMessages sm;
      id = sm.add(QStringLiteral("34600111222"), QStringLiteral("F"),
                  QStringLiteral("boom"),
                  QDateTime::currentDateTime().addSecs(3600));
      sm.reportResult(id, false, QStringLiteral("nope")); // Failed, kept on disk
    }
    ScheduledMessages sm2;
    bool found = false;
    for (const auto &e : sm2.entries())
      if (e.id == id) {
        QCOMPARE(e.status, ScheduledMessages::Status::Failed);
        found = true;
      }
    QVERIFY(found);
    sm2.remove(id);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Performance: the Chromium-flag fragment is a pure function of the stored
// settings, so it can be exercised directly. Runs in QStandardPaths test mode,
// so it writes to a throwaway settings file and restores every key it touches.
class TstPerformance : public QObject {
  Q_OBJECT
private slots:
  void init() {
    // Start from a clean, known slate for every case.
    Performance::setDisableGpu(false);
    Performance::setDisableGpuCompositing(false);
    Performance::setDisableGpuVsync(false);
    Performance::setInProcessGpu(false);
    Performance::setIgnoreGpuBlocklist(false);
    Performance::setSingleProcess(false);
    Performance::setProcessPerSite(false);
    Performance::setWebrtcShield(false);
    Performance::setJsMemoryLimitMb(0);
    Performance::setCacheType(QStringLiteral("disk"));
    Performance::setCacheMaxMb(0);
  }

  void emptyWhenAllOff() {
    QCOMPARE(Performance::chromiumFlagFragment(), QString());
  }

  void gpuFlagsMapCorrectly() {
    Performance::setDisableGpu(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--disable-gpu")));
    Performance::setDisableGpuCompositing(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--disable-gpu-compositing")));
    Performance::setInProcessGpu(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--in-process-gpu")));
    Performance::setIgnoreGpuBlocklist(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--ignore-gpu-blocklist")));
  }

  void processModelFlags() {
    Performance::setSingleProcess(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--single-process")));
    Performance::setProcessPerSite(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--process-per-site")));
  }

  void webrtcShieldFlag() {
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("webrtc")));
    Performance::setWebrtcShield(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(QLatin1String(
        "--force-webrtc-ip-handling-policy=disable_non_proxied_udp")));
  }

  void jsMemoryLimitFlag() {
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags")));
    Performance::setJsMemoryLimitMb(512);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags=--max-old-space-size=512")));
    // Negative values are clamped to 0 (no flag).
    Performance::setJsMemoryLimitMb(-5);
    QCOMPARE(Performance::jsMemoryLimitMb(), 0);
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags")));
  }

  void settersRoundTrip() {
    Performance::setDisableGpuVsync(true);
    QVERIFY(Performance::disableGpuVsync());
    Performance::setCacheType(QStringLiteral("memory"));
    QCOMPARE(Performance::cacheType(), QStringLiteral("memory"));
    Performance::setCacheMaxMb(256);
    QCOMPARE(Performance::cacheMaxMb(), 256);
    Performance::setCacheMaxMb(-1);
    QCOMPARE(Performance::cacheMaxMb(), 0);
  }

  void applyToProfileIsSafe() {
    // Must not crash on a null profile, and must set the cache type on a real one.
    // A named (persistent) profile is used: off-the-record profiles force
    // MemoryHttpCache and would ignore the disk/none choices.
    Performance::applyToProfile(nullptr);
    QWebEngineProfile profile(QStringLiteral("tst_perf"));
    Performance::setCacheType(QStringLiteral("memory"));
    Performance::applyToProfile(&profile);
    QCOMPARE(profile.httpCacheType(), QWebEngineProfile::MemoryHttpCache);
    Performance::setCacheType(QStringLiteral("none"));
    Performance::applyToProfile(&profile);
    QCOMPARE(profile.httpCacheType(), QWebEngineProfile::NoCache);
    Performance::setCacheType(QStringLiteral("disk"));
    Performance::applyToProfile(&profile);
    QCOMPARE(profile.httpCacheType(), QWebEngineProfile::DiskHttpCache);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// CustomJs: the addon manager stores files, tracks per-addon enabled state, and
// builds a combined guarded script. Runs in QStandardPaths test mode.
class TstCustomJs : public QObject {
  Q_OBJECT
private:
  QString writeTemp(const QByteArray &body) {
    auto *f = new QTemporaryFile(QDir::tempPath() +
                                 QStringLiteral("/whatly_addon_XXXXXX.js"));
    f->setAutoRemove(false);
    f->open();
    f->write(body);
    f->close();
    const QString p = f->fileName();
    delete f;
    return p;
  }
  void clearAll() {
    for (const auto &a : CustomJs::addons())
      CustomJs::remove(a.name);
  }
private slots:
  void init() { clearAll(); }
  void cleanup() { clearAll(); }

  void addEnablesAndAppears() {
    QVERIFY(CustomJs::addons().isEmpty());
    QVERIFY(!CustomJs::isActive());
    QString err;
    const QString name =
        CustomJs::addFromFile(writeTemp("console.log('hi');"), &err);
    QVERIFY2(!name.isEmpty(), qPrintable(err));
    QCOMPARE(CustomJs::addons().size(), 1);
    QVERIFY(CustomJs::isEnabled(name));
    QVERIFY(CustomJs::isActive());
    QVERIFY(CustomJs::scriptSource().contains(QLatin1String("console.log('hi')")));
    // Each addon is wrapped in its own try/catch.
    QVERIFY(CustomJs::scriptSource().contains(QLatin1String("try{")));
  }

  void disableDropsItFromTheScript() {
    QString err;
    const QString name = CustomJs::addFromFile(writeTemp("var x=1;"), &err);
    QVERIFY(!name.isEmpty());
    CustomJs::setEnabled(name, false);
    QVERIFY(!CustomJs::isEnabled(name));
    QVERIFY(!CustomJs::isActive());
    QVERIFY(!CustomJs::scriptSource().contains(QLatin1String("var x=1")));
    // Still listed (disabled, not removed).
    QCOMPARE(CustomJs::addons().size(), 1);
  }

  void removeDeletesIt() {
    QString err;
    const QString name = CustomJs::addFromFile(writeTemp("void 0;"), &err);
    QVERIFY(!name.isEmpty());
    CustomJs::remove(name);
    QVERIFY(CustomJs::addons().isEmpty());
    QVERIFY(CustomJs::sourceOf(name).isEmpty());
  }

  void nameCollisionGetsUniqueSuffix() {
    QString err;
    const QString a =
        CustomJs::addFromFile(writeTemp("1;"), &err, QStringLiteral("dup"));
    const QString b =
        CustomJs::addFromFile(writeTemp("2;"), &err, QStringLiteral("dup"));
    QCOMPARE(a, QStringLiteral("dup"));
    QVERIFY(b != a);
    QCOMPARE(CustomJs::addons().size(), 2);
  }

  void rejectsMissingFile() {
    QString err;
    QVERIFY(CustomJs::addFromFile(QStringLiteral("/no/such.js"), &err).isEmpty());
    QVERIFY(!err.isEmpty());
  }

  void installOnProfile() {
    QString err;
    CustomJs::addFromFile(writeTemp("window.__whatly_test=1;"), &err);
    QWebEngineProfile profile(QStringLiteral("tst_customjs"));
    CustomJs::install(&profile);
    const auto found = profile.scripts()->find(QStringLiteral("whatly-custom-js"));
    QCOMPARE(found.size(), 1);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Backup: the archive/extract and recursive-copy primitives (round-trip on temp
// dirs, using the system tar).
class TstBackup : public QObject {
  Q_OBJECT
private slots:
  void copyRecursivePreservesTree() {
    QTemporaryDir src, dst;
    QVERIFY(src.isValid() && dst.isValid());
    QVERIFY(QDir(src.path()).mkpath(QStringLiteral("a/b")));
    QFile f(src.filePath(QStringLiteral("a/b/c.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();
    QString err;
    QVERIFY2(Backup::copyDirRecursive(src.path(), dst.path(), &err),
             qPrintable(err));
    QFile out(dst.filePath(QStringLiteral("a/b/c.txt")));
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), QByteArray("hello"));
  }

  void archiveRoundTrip() {
    QTemporaryDir src, out, back;
    QVERIFY(src.isValid() && out.isValid() && back.isValid());
    QFile f(src.filePath(QStringLiteral("data.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("payload-123");
    f.close();
    const QString archive = out.filePath(QStringLiteral("p.tar.gz"));
    QString err;
    QVERIFY2(Backup::makeArchive(src.path(), archive, &err), qPrintable(err));
    QVERIFY(QFileInfo(archive).size() > 0);
    QVERIFY2(Backup::extractArchive(archive, back.path(), &err), qPrintable(err));
    QFile r(back.filePath(QStringLiteral("data.txt")));
    QVERIFY(r.open(QIODevice::ReadOnly));
    QCOMPARE(r.readAll(), QByteArray("payload-123"));
  }

  void archiveMissingSourceFails() {
    QString err;
    QVERIFY(!Backup::makeArchive(QStringLiteral("/no/such/dir"),
                                 QStringLiteral("/tmp/whatly-x.tar.gz"), &err));
    QVERIFY(!err.isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Shortcuts: the registry, override round-trip, reset and conflict detection.
class TstShortcuts : public QObject {
  Q_OBJECT
private slots:
  void init() {
    Shortcuts::clearRegistryForTest();
    Shortcuts::registerAction("reload", "Reload", QKeySequence(Qt::Key_F5));
    Shortcuts::registerAction("lock", "Lock",
                              QKeySequence(Qt::CTRL | Qt::Key_L));
    // Clear any stored overrides from a previous run.
    Shortcuts::reset("reload");
    Shortcuts::reset("lock");
  }
  void defaultsAndOverride() {
    QCOMPARE(Shortcuts::get("reload"), QKeySequence(Qt::Key_F5));
    Shortcuts::set("reload", QKeySequence(Qt::CTRL | Qt::Key_R));
    QCOMPARE(Shortcuts::get("reload"), QKeySequence(Qt::CTRL | Qt::Key_R));
    Shortcuts::reset("reload");
    QCOMPARE(Shortcuts::get("reload"), QKeySequence(Qt::Key_F5));
  }
  void conflictDetection() {
    // Nothing else uses Ctrl+R yet.
    QVERIFY(Shortcuts::conflictId("reload",
                                  QKeySequence(Qt::CTRL | Qt::Key_R)).isEmpty());
    // "lock" holds Ctrl+L → assigning it to "reload" conflicts with "lock".
    QCOMPARE(Shortcuts::conflictId("reload", QKeySequence(Qt::CTRL | Qt::Key_L)),
             QStringLiteral("lock"));
    // Its own current sequence is not a conflict against itself.
    QVERIFY(Shortcuts::conflictId("lock", QKeySequence(Qt::CTRL | Qt::Key_L))
                .isEmpty());
    // An empty sequence never conflicts.
    QVERIFY(Shortcuts::conflictId("reload", QKeySequence()).isEmpty());
  }
  void registeredListsInOrder() {
    const auto all = Shortcuts::registered();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.first().id, QStringLiteral("reload"));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// StorageInfo: recursive directory size and the human-readable formatter.
class TstStorageInfo : public QObject {
  Q_OBJECT
private slots:
  void humanReadableUnits() {
    QCOMPARE(StorageInfo::humanReadable(0), QStringLiteral("0 B"));
    QCOMPARE(StorageInfo::humanReadable(512), QStringLiteral("512 B"));
    QCOMPARE(StorageInfo::humanReadable(1024), QStringLiteral("1.0 KB"));
    QCOMPARE(StorageInfo::humanReadable(1536), QStringLiteral("1.5 KB"));
    QCOMPARE(StorageInfo::humanReadable(1024 * 1024), QStringLiteral("1.0 MB"));
    QCOMPARE(StorageInfo::humanReadable(-5), QStringLiteral("0 B"));
  }
  void directorySizeSumsFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
    auto write = [](const QString &p, int n) {
      QFile f(p);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray(n, 'x'));
    };
    write(dir.filePath(QStringLiteral("a.bin")), 100);
    write(dir.filePath(QStringLiteral("sub/b.bin")), 250);
    QCOMPARE(StorageInfo::directorySize(dir.path()), qint64(350));
    QCOMPARE(StorageInfo::directorySize(QStringLiteral("/no/such/dir")),
             qint64(0));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// UpdateCheck: version comparison and release-JSON parsing (no network).
class TstUpdateCheck : public QObject {
  Q_OBJECT
private slots:
  void compareOrders() {
    QVERIFY(UpdateCheck::compareVersions("6.3.0", "6.3.1") < 0);
    QVERIFY(UpdateCheck::compareVersions("6.3.1", "6.3.0") > 0);
    QCOMPARE(UpdateCheck::compareVersions("6.3.0", "6.3.0"), 0);
    QVERIFY(UpdateCheck::compareVersions("6.10.0", "6.9.0") > 0); // numeric, not lexical
  }
  void ignoresLeadingVAndMissingParts() {
    QCOMPARE(UpdateCheck::compareVersions("v6.3.0", "6.3.0"), 0);
    QCOMPARE(UpdateCheck::compareVersions("6.3", "6.3.0"), 0);
    QVERIFY(UpdateCheck::compareVersions("6.4", "6.3.9") > 0);
  }
  void toleratesSuffixes() {
    QCOMPARE(UpdateCheck::compareVersions("6.3.0-rc1", "6.3.0"), 0);
  }
  void parsesReleaseJson() {
    const QByteArray json =
        R"({"tag_name":"v6.4.0","html_url":"https://example/rel/v6.4.0"})";
    QString url;
    QCOMPARE(UpdateCheck::latestFromJson(json, &url), QStringLiteral("v6.4.0"));
    QCOMPARE(url, QStringLiteral("https://example/rel/v6.4.0"));
    QVERIFY(UpdateCheck::latestFromJson("{}", &url).isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fuzzy: the command-palette matcher. Subsequence matching, ranking, and the
// non-match / empty-query cases.
class TstFuzzy : public QObject {
  Q_OBJECT
private slots:
  void emptyQueryMatchesAll() {
    QCOMPARE(Fuzzy::score(QString(), QStringLiteral("anything")), 0);
  }
  void nonSubsequenceIsRejected() {
    QVERIFY(Fuzzy::score(QStringLiteral("xyz"), QStringLiteral("reload")) < 0);
    QVERIFY(Fuzzy::score(QStringLiteral("zzz"), QStringLiteral("")) < 0);
  }
  void subsequenceMatches() {
    QVERIFY(Fuzzy::score(QStringLiteral("rl"), QStringLiteral("Reload")) >= 0);
    QVERIFY(Fuzzy::score(QStringLiteral("tog"), QStringLiteral("Toggle theme")) >= 0);
  }
  void caseInsensitive() {
    QVERIFY(Fuzzy::score(QStringLiteral("REL"), QStringLiteral("reload")) >= 0);
  }
  void contiguousBeatsScattered() {
    const int contiguous = Fuzzy::score(QStringLiteral("set"), QStringLiteral("Settings"));
    const int scattered = Fuzzy::score(QStringLiteral("set"), QStringLiteral("Save exit tab"));
    QVERIFY(contiguous > scattered);
  }
  void wordStartBonus() {
    // "at" as a word-start ("Add Tab") should beat "at" mid-word ("Waterfall").
    const int wordStart = Fuzzy::score(QStringLiteral("at"), QStringLiteral("Add Tab"));
    const int midWord = Fuzzy::score(QStringLiteral("at"), QStringLiteral("Waterfall"));
    QVERIFY(wordStart > midWord);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// NotificationRules: shouldNotify() is a pure function of the stored rules, so
// the DND window (including wrap-around) and keyword override are testable.
class TstNotificationRules : public QObject {
  Q_OBJECT
private slots:
  void init() {
    NotificationRules::setDndEnabled(false);
    NotificationRules::setDndStart(QStringLiteral("22:00"));
    NotificationRules::setDndEnd(QStringLiteral("08:00"));
    NotificationRules::setKeywords({});
  }

  QDateTime at(int h, int m) {
    return QDateTime(QDate(2026, 1, 1), QTime(h, m));
  }

  void notifiesWhenDndOff() {
    QVERIFY(NotificationRules::shouldNotify(at(3, 0), "A", "hello"));
  }

  void wrapAroundWindowSuppresses() {
    NotificationRules::setDndEnabled(true); // 22:00 → 08:00
    QVERIFY(NotificationRules::inDndWindow(at(23, 0)));
    QVERIFY(NotificationRules::inDndWindow(at(2, 0)));
    QVERIFY(!NotificationRules::inDndWindow(at(12, 0)));
    QVERIFY(!NotificationRules::shouldNotify(at(2, 0), "A", "hi"));
    QVERIFY(NotificationRules::shouldNotify(at(12, 0), "A", "hi"));
  }

  void sameDayWindow() {
    NotificationRules::setDndEnabled(true);
    NotificationRules::setDndStart(QStringLiteral("09:00"));
    NotificationRules::setDndEnd(QStringLiteral("17:00"));
    QVERIFY(NotificationRules::inDndWindow(at(12, 0)));
    QVERIFY(!NotificationRules::inDndWindow(at(20, 0)));
  }

  void keywordBreaksThroughDnd() {
    NotificationRules::setDndEnabled(true); // 22:00 → 08:00
    NotificationRules::setKeywords({QStringLiteral("urgent"), QStringLiteral("boss")});
    QVERIFY(NotificationRules::matchesKeyword("Msg", "this is URGENT"));
    QVERIFY(NotificationRules::shouldNotify(at(2, 0), "Msg", "this is urgent"));
    QVERIFY(!NotificationRules::shouldNotify(at(2, 0), "Msg", "nothing here"));
  }

  void zeroLengthWindowNeverSuppresses() {
    NotificationRules::setDndEnabled(true);
    NotificationRules::setDndStart(QStringLiteral("10:00"));
    NotificationRules::setDndEnd(QStringLiteral("10:00"));
    QVERIFY(!NotificationRules::inDndWindow(at(10, 0)));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// NetworkProxy: the stored mode/host/port round-trip and applyToApplication()
// installs the right QNetworkProxy. Runs in QStandardPaths test mode.
class TstNetworkProxy : public QObject {
  Q_OBJECT
private slots:
  void init() {
    NetworkProxy::setMode(QStringLiteral("system"));
    NetworkProxy::setHost(QString());
    NetworkProxy::setPort(0);
    NetworkProxy::setUser(QString());
    NetworkProxy::setPassword(QString());
  }

  void defaultsToSystem() {
    QCOMPARE(NetworkProxy::mode(), QStringLiteral("system"));
  }

  void settersRoundTrip() {
    NetworkProxy::setMode(QStringLiteral("socks5"));
    NetworkProxy::setHost(QStringLiteral("10.0.0.1"));
    NetworkProxy::setPort(1080);
    NetworkProxy::setUser(QStringLiteral("bob"));
    NetworkProxy::setPassword(QStringLiteral("secret"));
    QCOMPARE(NetworkProxy::mode(), QStringLiteral("socks5"));
    QCOMPARE(NetworkProxy::host(), QStringLiteral("10.0.0.1"));
    QCOMPARE(NetworkProxy::port(), 1080);
    QCOMPARE(NetworkProxy::user(), QStringLiteral("bob"));
    QCOMPARE(NetworkProxy::password(), QStringLiteral("secret"));
  }

  void portIsClamped() {
    NetworkProxy::setPort(99999);
    QCOMPARE(NetworkProxy::port(), 65535);
    NetworkProxy::setPort(-1);
    QCOMPARE(NetworkProxy::port(), 0);
  }

  void applyNoneGivesNoProxy() {
    NetworkProxy::setMode(QStringLiteral("none"));
    NetworkProxy::applyToApplication();
    QCOMPARE(QNetworkProxy::applicationProxy().type(), QNetworkProxy::NoProxy);
  }

  void applyManualGivesManualProxy() {
    NetworkProxy::setMode(QStringLiteral("http"));
    NetworkProxy::setHost(QStringLiteral("proxy.example"));
    NetworkProxy::setPort(3128);
    NetworkProxy::setUser(QStringLiteral("u"));
    NetworkProxy::setPassword(QStringLiteral("p"));
    NetworkProxy::applyToApplication();
    const QNetworkProxy p = QNetworkProxy::applicationProxy();
    QCOMPARE(p.type(), QNetworkProxy::HttpProxy);
    QCOMPARE(p.hostName(), QStringLiteral("proxy.example"));
    QCOMPARE(p.port(), quint16(3128));
    QCOMPARE(p.user(), QStringLiteral("u"));

    NetworkProxy::setMode(QStringLiteral("socks5"));
    NetworkProxy::applyToApplication();
    QCOMPARE(QNetworkProxy::applicationProxy().type(),
             QNetworkProxy::Socks5Proxy);
  }

  void cleanup() {
    // Leave the global application proxy in a neutral state for later tests.
    NetworkProxy::setMode(QStringLiteral("system"));
    NetworkProxy::applyToApplication();
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Autostart: enabling writes an entry, disabling removes it. In QStandardPaths
// test mode the XDG autostart dir is a throwaway path, so this is side-effect
// free on the developer's real session.
class TstAutostart : public QObject {
  Q_OBJECT
private slots:
  void toggleRoundTrip() {
    if (!Autostart::isSupported())
      QSKIP("autostart not implemented on this platform");
    Autostart::setEnabled(false);
    QVERIFY(!Autostart::isEnabled());
    QVERIFY(Autostart::setEnabled(true));
    QVERIFY(Autostart::isEnabled());
    QVERIFY(Autostart::setEnabled(false));
    QVERIFY(!Autostart::isEnabled());
  }
  void disableWhenAlreadyOffSucceeds() {
    if (!Autostart::isSupported())
      QSKIP("autostart not implemented on this platform");
    Autostart::setEnabled(false);
    QVERIFY(Autostart::setEnabled(false));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// PortalNotification: the sandbox-detection and graceful-degradation logic is
// testable headlessly. The D-Bus round-trip itself needs a live portal, so it is
// only asserted to fail safely when none is present.
class TstPortalNotification : public QObject {
  Q_OBJECT
private slots:
  void inFlatpakFollowsEnvironment() {
    const bool had = qEnvironmentVariableIsSet("FLATPAK_ID");
    const QByteArray old = qgetenv("FLATPAK_ID");
    qputenv("FLATPAK_ID", "org.example.Test");
    QVERIFY(PortalNotification::inFlatpak());
    qunsetenv("FLATPAK_ID");
    // /.flatpak-info may still exist on a real Flatpak host; only assert the
    // negative when we know we are not in one.
    if (!QFile::exists(QStringLiteral("/.flatpak-info")))
      QVERIFY(!PortalNotification::inFlatpak());
    if (had)
      qputenv("FLATPAK_ID", old);
  }

  void sendFailsGracefullyWhenUnavailable() {
    if (PortalNotification::isAvailable())
      QSKIP("a real portal is present; cannot assert the unavailable path");
    PortalNotification n;
    // Must not crash and must report failure when there is no portal.
    QVERIFY(!n.send(QStringLiteral("id-1"), QStringLiteral("t"),
                    QStringLiteral("b")));
    n.remove(QStringLiteral("id-1")); // no-op, must not crash
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// SetupWizard: the first-run flag round-trips, and applyChoices() persists the
// chosen options. Constructible headlessly since it only touches settings.
class TstSetupWizard : public QObject {
  Q_OBJECT
private slots:
  void completionFlagRoundTrips() {
    SettingsManager::instance().settings().setValue("setupWizardCompleted",
                                                    false);
    QVERIFY(!SetupWizard::isCompleted());
    SetupWizard::markCompleted();
    QVERIFY(SetupWizard::isCompleted());
  }

  void constructsAndAppliesChoices() {
    SettingsManager::instance().settings().setValue("setupWizardCompleted",
                                                    false);
    SetupWizard wizard;
    // Applying should mark the wizard completed without needing exec().
    wizard.applyChoices();
    QVERIFY(SetupWizard::isCompleted());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// install() paths: give each injected-script module a real (headless) profile.
class TstScriptInstall : public QObject {
  Q_OBJECT
private slots:
  void installOnProfile() {
    // Turn every feature on first, so install() takes the script-inserting path
    // rather than its early "nothing to do" return.
    WebFont::setCurrentFamily(QStringLiteral("DejaVu Sans"));
    MutedStatus::setEnabled(true);
    if (!ChatTheme::themes().isEmpty())
      ChatTheme::setCurrentThemeId(ChatTheme::themes().last().id);
    if (!PrivacyBlur::levels().isEmpty())
      PrivacyBlur::setCurrentLevelId(PrivacyBlur::levels().last().id);

    QTemporaryFile css;
    css.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_XXXXXX.css"));
    QVERIFY(css.open());
    css.write("body{background:#000}");
    css.close();
    QString err;
    CustomCss::setFromFile(css.fileName(), &err);

    QTemporaryDir wpDir;
    const QString wp = wpDir.filePath(QStringLiteral("wp.png"));
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::black);
    QVERIFY(img.save(wp));
    ChatWallpaper::setImage(wp, &err);

    QWebEngineProfile profile(QStringLiteral("whatly-test-profile"));
    const int before = profile.scripts()->count();
    WebFont::install(&profile);
    ChatTheme::install(&profile);
    MutedStatus::install(&profile);
    PrivacyBlur::install(&profile);
    ChatWallpaper::install(&profile);
    CustomCss::install(&profile);
    WebTweaks::install(&profile);
    LinkedDeviceName::install(&profile, QStringLiteral("Work"));
    QVERIFY(profile.scripts()->count() > before);

    // Now turn everything off and install again: this exercises each module's
    // other branch — remove the previously-inserted script, then early-return.
    MutedStatus::setEnabled(false);
    WebFont::setCurrentFamily(QString());
    CustomCss::clear();
    ChatWallpaper::clear();
    if (!ChatTheme::themes().isEmpty())
      ChatTheme::setCurrentThemeId(ChatTheme::themes().first().id); // default/off
    if (!PrivacyBlur::levels().isEmpty())
      PrivacyBlur::setCurrentLevelId(PrivacyBlur::levels().first().id);
    WebFont::install(&profile);
    ChatTheme::install(&profile);
    MutedStatus::install(&profile);
    PrivacyBlur::install(&profile);
    ChatWallpaper::install(&profile);
    CustomCss::install(&profile);
  }
};

int main(int argc, char *argv[]) {
  // Keep the (headless) QWebEngineProfile used by the install() test happy on CI
  // runners: no sandbox, no GPU. Must be set before QApplication.
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox --disable-gpu");
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
  QCoreApplication::setApplicationName(QStringLiteral("whatly-test"));
  // Keep test settings out of the real config.
  QStandardPaths::setTestModeEnabled(true);

  int status = 0;
  auto run = [&](QObject *obj) { status |= QTest::qExec(obj, argc, argv); };
  { TstUtils t;               run(&t); }
  { TstUtilsMore t;           run(&t); }
  { TstCommon t;              run(&t); }
  { TstDebugLog t;            run(&t); }
  { TstIdenticons t;          run(&t); }
  { TstTheme t;               run(&t); }
  { TstDictionaries t;        run(&t); }
  { TstSunclock t;            run(&t); }
  { TstScheduled t;           run(&t); }
  { TstScheduledPersistence t; run(&t); }
  { TstScripts t;             run(&t); }
  { TstCustomCss t;           run(&t); }
  { TstCustomJs t;            run(&t); }
  { TstChatWallpaper t;       run(&t); }
  { TstPerformance t;         run(&t); }
  { TstShortcuts t;           run(&t); }
  { TstBackup t;              run(&t); }
  { TstStorageInfo t;         run(&t); }
  { TstUpdateCheck t;         run(&t); }
  { TstFuzzy t;               run(&t); }
  { TstNotificationRules t;   run(&t); }
  { TstNetworkProxy t;        run(&t); }
  { TstAutostart t;           run(&t); }
  { TstPortalNotification t;  run(&t); }
  { TstSetupWizard t;         run(&t); }
  { TstScriptInstall t;       run(&t); }
  // Profile-mutating test runs last so it doesn't disturb the others.
  { TstAppProfile t;          run(&t); }
  { TstAppProfileArgs t;      run(&t); }
  return status;
}

#include "tst_logic.moc"
