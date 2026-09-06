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
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QMimeData>
#include <QTemporaryFile>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>
#include <QDirIterator>
#include <QRegularExpression>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QSet>

#include "mediastuck.h"
#include "utils.h"
#include "common.h"
#include "debuglog.h"
#include "appprofile.h"
#include "settingsmanager.h"
#include "dictionaries.h"
#include "dictionarybootstrap.h"
#include "dictionarymanager.h"
#include "dictionaryrows.h"
#include "identicons.h"
#include "theme.h"
#include "settingssearch.h"
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
#include "sessionbackup.h"
#include "screenlock.h"
#include "quickreply.h"
#include "focusmode.h"
#include "hdmedia.h"
#include "undosend.h"
#include "translator.h"
#include "chatexport.h"
#include "notificationreply.h"
#include "aiassistant.h"
#include "ollama.h"
#include "passlock.h"
#include "cannedresponses.h"
#include "webtweaks.h"
#include "chatliststrip.h"
#include "messaging.h"
#include "messagetemplates.h"
#include "autoreply.h"
#include "cloudapi.h"
#include "localapi.h"
#include "cloudwebhook.h"
#include <QMessageAuthenticationCode>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "linkeddevicename.h"
#include "accounttabbar.h"
#include "performance.h"
#include "trayicon.h"
#include "accounttabbar.h"
#include "chatnav.h"
#include "dropattach.h"
#include "dropreader.h"
#include "dropresolve.h"
#include "lingertip.h"
#include "networkproxy.h"
#include "notificationrules.h"
#include "autostart.h"
#include "portalnotification.h"
#include "setupwizard.h"
#include "settingsmanager.h"

#include <QNetworkProxy>

#ifdef Q_OS_WIN
// MSVC has no timegm(); _mkgmtime is the same function under another name.
// Without this the whole logic suite fails to compile on Windows, so none of it
// runs there at all — ctest just reports "Not Run".
#include <ctime>
#define timegm _mkgmtime
#endif

// ─────────────────────────────────────────────────────────────────────────────
class TstUtils : public QObject {
  Q_OBJECT
private slots:
  void whatsAppLoadFailure() {
    // The real cascade from issue #43 carries both markers -> detected.
    const QString blob = QStringLiteral(
        "Requiring module \"WAWebMiscBrowserUtils\" with unresolved "
        "dependencies:\nWAWebUserPrefsGeneral is waiting for ...\n"
        "cr:34987 is not defined");
    QVERIFY(Utils::isWhatsAppLoadFailure(blob));
    // Ordinary page errors, or only one marker, must not trip it.
    QVERIFY(!Utils::isWhatsAppLoadFailure(
        QStringLiteral("Uncaught TypeError: foo is not a function")));
    QVERIFY(!Utils::isWhatsAppLoadFailure(
        QStringLiteral("bar is not defined")));
    QVERIFY(!Utils::isWhatsAppLoadFailure(
        QStringLiteral("module has unresolved dependencies")));
    QVERIFY(!Utils::isWhatsAppLoadFailure(QString()));
    // Verbatim from a load that went on to succeed: a named module waiting for
    // another one that has not arrived yet. Both phrases are there, no module id
    // is, and the page was up seconds later — so this must stay quiet.
    QVERIFY(!Utils::isWhatsAppLoadFailure(QStringLiteral(
        "Requiring module \"WAWebMiscBrowserUtils\" with unresolved "
        "dependencies: WAWebMiscBrowserUtils is waiting for "
        "WAWebUserPrefsGeneral\nWAWebUserPrefsGeneral is not defined")));
    QVERIFY(!Utils::isWhatsAppLoadFailure(QStringLiteral(
        "Requiring module \"WAWebCompanionRegClientUtils\" with unresolved "
        "dependencies: WAWebCompanionRegClientUtils is not defined")));
  }
  void benignWebConsoleNoise() {
    // WhatsApp's telemetry beacon, CORS-blocked -> noise.
    QVERIFY(Utils::isBenignWebConsoleNoise(QStringLiteral(
        "Access to fetch at 'https://dit.whatsapp.net/deidentified_telemetry' "
        "from origin 'https://web.whatsapp.com' has been blocked by CORS "
        "policy: No 'Access-Control-Allow-Origin' header is present")));
    // Permissions-Policy naming a feature this Chromium lacks -> noise.
    QVERIFY(Utils::isBenignWebConsoleNoise(QStringLiteral(
        "Error with Permissions-Policy header: Unrecognized feature: "
        "'bluetooth'.")));
    // A reason-less promise rejection -> noise (WhatsApp floods these).
    QVERIFY(Utils::isBenignWebConsoleNoise(
        QStringLiteral("Uncaught (in promise) undefined")));
    // A real CORS failure to some other endpoint must still surface.
    QVERIFY(!Utils::isBenignWebConsoleNoise(QStringLiteral(
        "Access to fetch at 'https://api.example.com/data' has been blocked "
        "by CORS policy")));
    // A promise rejection that carries an actual value must still surface.
    QVERIFY(!Utils::isBenignWebConsoleNoise(QStringLiteral(
        "Uncaught (in promise) TypeError: x is not a function")));
    QVERIFY(!Utils::isBenignWebConsoleNoise(QString()));
  }
  void serviceWorkerRegistrationFailure() {
    QVERIFY(Utils::isServiceWorkerRegistrationFailure(QStringLiteral(
        "Failed to register a ServiceWorker for scope "
        "('https://web.whatsapp.com/') with script ...: ServiceWorker script "
        "evaluation failed")));
    QVERIFY(Utils::isServiceWorkerRegistrationFailure(QStringLiteral(
        "[Caught in: service-worker-registration-failure "
        "(service-worker-registration-failure)]")));
    QVERIFY(!Utils::isServiceWorkerRegistrationFailure(
        QStringLiteral("Uncaught TypeError: foo is not a function")));
    QVERIFY(!Utils::isServiceWorkerRegistrationFailure(QString()));
  }
  // Arm the Wayland RHI-failure watch (and possible one-shot XCB relaunch) only
  // on a Wayland session the user did not override, and never on the relaunch
  // that already switched to XCB (issue #84).
  void waylandRhiFallbackPolicy() {
    // Wayland, not overridden, first attempt: arm it.
    QVERIFY(Utils::shouldArmWaylandRhiFallback(false, true, false));
    // The user's own platform choice always wins.
    QVERIFY(!Utils::shouldArmWaylandRhiFallback(true, true, false));
    // Not a Wayland session: nothing to fall back from.
    QVERIFY(!Utils::shouldArmWaylandRhiFallback(false, false, false));
    // Already the XCB fallback attempt: never arm again (no relaunch loop).
    QVERIFY(!Utils::shouldArmWaylandRhiFallback(false, true, true));
  }
  // Offer the in-place AppImage update only when it is an AppImage, the tool is
  // present, and the running image path is known (issue #85).
  void appImageSelfUpdatePolicy() {
    QVERIFY(Utils::canOfferAppImageSelfUpdate(
        true, QStringLiteral("/usr/bin/appimageupdatetool"),
        QStringLiteral("/home/u/Whatly.AppImage")));
    // Not an AppImage: never.
    QVERIFY(!Utils::canOfferAppImageSelfUpdate(
        false, QStringLiteral("/usr/bin/appimageupdatetool"),
        QStringLiteral("/home/u/Whatly.AppImage")));
    // Tool missing, or image path unknown: cannot run it, so do not offer.
    QVERIFY(!Utils::canOfferAppImageSelfUpdate(
        true, QString(), QStringLiteral("/home/u/Whatly.AppImage")));
    QVERIFY(!Utils::canOfferAppImageSelfUpdate(
        true, QStringLiteral("/usr/bin/appimageupdatetool"), QString()));
  }
  void mediaThatNeverArrives() {
    // What the injected watcher prints, verbatim in shape.
    const QString asked =
        QStringLiteral("WHATLY_MEDIA_STUCK {\"attempts\":2,\"online\":true}");
    QVERIFY(MediaStuck::isReport(asked));
    QCOMPARE(MediaStuck::parse(asked).attempts, 2);
    QVERIFY(MediaStuck::parse(asked).online);
    // Anything else is not one, and parses to nothing worth saying.
    QVERIFY(!MediaStuck::isReport(QStringLiteral("Uncaught TypeError: x")));
    QVERIFY(!MediaStuck::isReport(QString()));
    QCOMPARE(MediaStuck::parse(QStringLiteral("noise")).attempts, 0);

    // One ask is not a failure: WhatsApp fetches lazily and the first click
    // often just takes a moment. Two with a connection means the copy is
    // probably gone; two without one means it will come by itself.
    QCOMPARE(MediaStuck::adviceFor({1, true}), MediaStuck::Advice::None);
    QCOMPARE(MediaStuck::adviceFor({0, false}), MediaStuck::Advice::None);
    QCOMPARE(MediaStuck::adviceFor({2, true}), MediaStuck::Advice::Expired);
    QCOMPARE(MediaStuck::adviceFor({3, false}), MediaStuck::Advice::Offline);

    // Every advice that is given has something to say; None says nothing.
    QVERIFY(!MediaStuck::text(MediaStuck::Advice::Expired).isEmpty());
    QVERIFY(!MediaStuck::text(MediaStuck::Advice::Offline).isEmpty());
    QVERIFY(MediaStuck::text(MediaStuck::Advice::None).isEmpty());
    // The two sentences must differ: the offline one promises the file arrives
    // on its own, which would be a lie in the other case.
    QVERIFY(MediaStuck::text(MediaStuck::Advice::Expired) !=
            MediaStuck::text(MediaStuck::Advice::Offline));

    // The watcher goes in once and prints the marker this side parses.
    const QString js = MediaStuck::watcherScript();
    QVERIFY(js.contains(QLatin1String("__whatlyMediaWatch")));
    QVERIFY(js.contains(QString(MediaStuck::kMarker).trimmed()));
  }
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
  // The version shown in the window title and beside the tabs. This binary is
  // compiled with VERSIONSTR="test" and WITHOUT a build label, which is the case
  // the #ifdef in versionLabel() exists for — an unguarded use of the macro would
  // not compile here at all.
  void versionLabelForTheTitle() {
    QCOMPARE(Utils::versionLabel(), QStringLiteral("test"));
    // The name leads and the version follows, so shortening a title in a narrow
    // title bar takes the version rather than the app's name.
    QVERIFY(Utils::appNameWithVersion().endsWith(QStringLiteral(" test")));
    QVERIFY(!Utils::appNameWithVersion().startsWith(QStringLiteral("test")));
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
class TstSettingsSearch : public QObject {
  Q_OBJECT
private slots:
  // What the Settings search will and will not find (#39). The page-filtering
  // half is in tst_settings, which can build the real page; this is the part with
  // the rules in it.
  void matchingIsForgiving() {
    // Case, accents and punctuation are all beside the point: what matters is
    // what someone can type without thinking about it.
    QVERIFY(SettingsSearch::matches(QStringLiteral("Básico"),
                                    QStringLiteral("basico")));
    QVERIFY(SettingsSearch::matches(QStringLiteral("Notificación"),
                                    QStringLiteral("NOTIFICACION")));
    QVERIFY(SettingsSearch::matches(QStringLiteral("Spell-check dictionaries"),
                                    QStringLiteral("spellcheck")));
    QVERIFY(SettingsSearch::matches(QStringLiteral("Wi-Fi"),
                                    QStringLiteral("wifi")));

    // Every word has to appear, in any order, and half a word counts while it is
    // being typed.
    QVERIFY(SettingsSearch::matches(
        QStringLiteral("Unload also minimised and hidden accounts"),
        QStringLiteral("hidden unload")));
    QVERIFY(SettingsSearch::matches(
        QStringLiteral("Unload also minimised and hidden accounts"),
        QStringLiteral("minim")));
    QVERIFY(!SettingsSearch::matches(
        QStringLiteral("Unload also minimised and hidden accounts"),
        QStringLiteral("unload proxy")));

    // An empty query matches everything: that is how clearing the box gives the
    // whole page back rather than emptying it.
    QVERIFY(SettingsSearch::matches(QStringLiteral("anything"), QString()));
    QVERIFY(SettingsSearch::matches(QStringLiteral("anything"),
                                    QStringLiteral("   ")));
  }

  // A row is found by everything on it — including the tooltip, which is where
  // the words people search with actually live: the label says "unload inactive
  // accounts" and only the tooltip says "memory".
  void rowsAreFoundByTheirTooltipsToo() {
    QGroupBox body;
    auto *column = new QVBoxLayout(&body);
    auto *lonely = new QCheckBox(QStringLiteral("Unload inactive accounts"));
    lonely->setToolTip(QStringLiteral("Free memory by unloading accounts"));
    column->addWidget(lonely);
    auto *pair = new QHBoxLayout;
    auto *label = new QLabel(QStringLiteral("Font hinting"));
    auto *combo = new QComboBox;
    combo->addItem(QStringLiteral("Slight"));
    pair->addWidget(label);
    pair->addWidget(combo);
    column->addLayout(pair);
    column->addStretch(1); // no widgets: not a row, and not a crash either

    const QList<SettingsSearch::Row> rows = SettingsSearch::rowsOf(&body);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].widgets.size(), 1);
    QVERIFY(SettingsSearch::matches(rows[0].haystack, QStringLiteral("memory")));

    // The label and the control it labels are one row, so a search can never
    // leave a label standing beside nothing or a spin box with no name.
    QCOMPARE(rows[1].widgets.size(), 2);
    QVERIFY(rows[1].widgets.contains(label));
    QVERIFY(rows[1].widgets.contains(combo));
    // And a combo is found by what is in it, not only by its label.
    QVERIFY(SettingsSearch::matches(rows[1].haystack, QStringLiteral("slight")));
  }

  // Dogfood round 28: searching "spelling" showed all forty keyboard shortcuts.
  // They are a form inside one host widget — one item of the section's column, and
  // so one row, however many lines it draws. A row has to be a band of the page,
  // which means walking down through anything that stacks.
  void aFormInsideAHostIsNotOneRow() {
    QGroupBox body;
    auto *column = new QVBoxLayout(&body);
    auto *host = new QWidget; // exactly what the shortcuts section uses
    auto *form = new QFormLayout(host);
    form->addRow(QStringLiteral("Spelling: next language"),
                 new QLineEdit(QStringLiteral("Ctrl+Alt+S")));
    form->addRow(QStringLiteral("Find in chats"),
                 new QLineEdit(QStringLiteral("Ctrl+F")));
    form->addRow(QStringLiteral("Quit"), new QLineEdit(QStringLiteral("Ctrl+Q")));
    column->addWidget(host);

    const QList<SettingsSearch::Row> rows = SettingsSearch::rowsOf(&body);
    QCOMPARE(rows.size(), 3);
    // Label and field together, and the host recorded as the group they are in,
    // so it can be hidden when none of them matches.
    for (const SettingsSearch::Row &row : rows) {
      QCOMPARE(row.widgets.size(), 2);
      QCOMPARE(row.container, host);
    }
    int found = 0;
    for (const SettingsSearch::Row &row : rows)
      if (SettingsSearch::matches(row.haystack, QStringLiteral("spelling")))
        ++found;
    QCOMPARE(found, 1); // one shortcut, not the whole list
  }

  // The same for a grid, which is how the older half of the page is laid out.
  void aGridSplitsByItsRows() {
    QGroupBox body;
    auto *grid = new QGridLayout(&body);
    grid->addWidget(new QLabel(QStringLiteral("Interface language")), 0, 0);
    grid->addWidget(new QComboBox, 0, 1);
    grid->addWidget(new QCheckBox(QStringLiteral("Use native file dialog")), 1, 0);

    const QList<SettingsSearch::Row> rows = SettingsSearch::rowsOf(&body);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].widgets.size(), 2); // the label and the control it labels
    QCOMPARE(rows[1].widgets.size(), 1);
    QVERIFY(SettingsSearch::matches(rows[0].haystack, QStringLiteral("language")));
    QVERIFY(!SettingsSearch::matches(rows[1].haystack, QStringLiteral("language")));
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

  // A greyed-out control has to look greyed out in both themes. Every role a
  // label can be drawn in, because setColor() without a colour group sets the
  // disabled group as well — so forgetting one leaves disabled text in the
  // enabled colour, which is what had happened to WindowText, and so to every
  // check box, in the dark palette.
  //
  // Assert dimness, not mere inequality: the disabled foreground must have less
  // contrast against its own background than the active one does, so a colour
  // that differs but is not actually dimmer cannot pass. Each foreground role is
  // paired with the background it is drawn on.
  void disabledIsDimmerThanEnabled() {
    auto luma = [](const QColor &c) {
      return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
    };
    auto contrast = [&](const QPalette &p, QPalette::ColorGroup g,
                        QPalette::ColorRole fg, QPalette::ColorRole bg) {
      return qAbs(luma(p.color(g, fg)) - luma(p.color(g, bg)));
    };
    const struct {
      QPalette::ColorRole fg, bg;
    } pairs[] = {{QPalette::WindowText, QPalette::Window},
                 {QPalette::Text, QPalette::Base},
                 {QPalette::ButtonText, QPalette::Button}};
    for (const QPalette &p : {Theme::getLightPalette(), Theme::getDarkPalette()})
      for (const auto &pair : pairs)
        QVERIFY(contrast(p, QPalette::Disabled, pair.fg, pair.bg) <
                contrast(p, QPalette::Active, pair.fg, pair.bg));
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

  // Round 28, his request: the interface-language list is now named by the same
  // routine as the spell-check list — Dictionaries::languageLabel — so the two
  // lists a reader compares call the same language by the same name. Over the
  // translations actually shipped, every name has to be its own: the old code
  // built the name from the language alone, which threw the territory away and
  // made zh_TW resolve to zh_CN's, so both Chinese translations were offered as
  // "简体中文" — simplified, on the traditional one too.
  void everyShippedInterfaceLanguageHasItsOwnName() {
    QDir dir(QStringLiteral(WHATLY_SOURCE_DIR) + QStringLiteral("/src/i18n"));
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.ts")}, QDir::Files, QDir::Name);
    QVERIFY(files.size() > 5); // the catalogues are there, so the check ran
    QSet<QString> seen;
    for (const QFileInfo &file : files) {
      const QString label = Dictionaries::languageLabel(file.completeBaseName());
      QVERIFY(!label.isEmpty());
      QVERIFY2(!seen.contains(label),
               qPrintable(QStringLiteral("two interface languages named ") + label));
      seen.insert(label);
    }
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
  // Focusing one of the chosen languages (#41): what Chromium is given, and the
  // order a key mid-sentence walks through.
  void focusOneChosenLanguage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &n : {"en_US", "es_ES", "eo"}) {
      QFile f(dir.filePath(n + QStringLiteral(".bdic")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("BDIC-stub");
      f.close();
    }
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dir.path().toLocal8Bit());
    const QStringList three{QStringLiteral("en_US"), QStringLiteral("eo"),
                            QStringLiteral("es_ES")};
    SettingsManager::instance().settings().setValue(
        QStringLiteral("spellCheckLanguages"), three);
    QCOMPARE(Dictionaries::selectedDictionaries(), three);

    // Nothing focused: every chosen language at once, as before.
    Dictionaries::setFocusedDictionary(QString());
    QVERIFY(Dictionaries::focusedDictionary().isEmpty());
    QCOMPARE(Dictionaries::activeDictionaries(), three);

    // Focused: that one alone, however many are ticked.
    Dictionaries::setFocusedDictionary(QStringLiteral("eo"));
    QCOMPARE(Dictionaries::focusedDictionary(), QStringLiteral("eo"));
    QCOMPARE(Dictionaries::activeDictionaries(),
             QStringList{QStringLiteral("eo")});

    // Focused on one that is no longer ticked: back to all of them, rather than
    // checking against a language that is not in the picker any more.
    SettingsManager::instance().settings().setValue(
        QStringLiteral("spellCheckLanguages"),
        QStringList{QStringLiteral("en_US"), QStringLiteral("es_ES")});
    QVERIFY(Dictionaries::focusedDictionary().isEmpty());
    QCOMPARE(Dictionaries::activeDictionaries().size(), 2);

    // One lap: each language in turn, then all of them, then round again.
    QCOMPARE(Dictionaries::nextFocus(three, QString()), QStringLiteral("en_US"));
    QCOMPARE(Dictionaries::nextFocus(three, QStringLiteral("en_US")),
             QStringLiteral("eo"));
    QCOMPARE(Dictionaries::nextFocus(three, QStringLiteral("eo")),
             QStringLiteral("es_ES"));
    QVERIFY(Dictionaries::nextFocus(three, QStringLiteral("es_ES")).isEmpty());
    // A language dropped from the picker since it was focused starts the lap over.
    QCOMPARE(Dictionaries::nextFocus(three, QStringLiteral("zz_ZZ")),
             QStringLiteral("en_US"));
    // Nothing to switch between: all of them is the only stop there is.
    QVERIFY(Dictionaries::nextFocus({QStringLiteral("eo")},
                                    QStringLiteral("eo"))
                .isEmpty());
    QVERIFY(Dictionaries::nextFocus({}, QString()).isEmpty());

    // Labels: one shape for every entry, the language in its own words and the
    // territory that says which variant it is — so a list of them reads as a list
    // rather than as an accident. tst_settings pins the pairs that used to collide.
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("es_ES")),
             QStringLiteral("español (España)"));
    QVERIFY(Dictionaries::languageLabel(QStringLiteral("eo"))
                .startsWith(QStringLiteral("Esperanto")));
    // An unknown code is left exactly as it is.
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("zz_ZZ")),
             QStringLiteral("zz_ZZ"));
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("zz_ZZ")),
             QStringLiteral("zz_ZZ"));

    SettingsManager::instance().settings().remove(
        QStringLiteral("spellCheckLanguages"));
    SettingsManager::instance().settings().remove(
        QStringLiteral("spellCheckFocus"));
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
  // syncDictionaryDirs mirrors the bundle into the user dir, keeps a user-added
  // dictionary, and drops whatever in it is not a dictionary. The bundled files
  // carry the real "BDic" magic because that is now what decides.
  void userDictSync() {
    QTemporaryDir bundle, user;
    QVERIFY(bundle.isValid() && user.isValid());
    for (const QString &n : {"en_US", "es_ES"}) {
      QFile f(bundle.filePath(n + QStringLiteral(".bdic")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("BDic");
      f.close();
    }
    // A dictionary the user dropped in themselves...
    QFile uf(user.filePath(QStringLiteral("zz_ZZ.bdic")));
    QVERIFY(uf.open(QIODevice::WriteOnly));
    uf.write("BDic");
    uf.close();
    // ...and something a previous run left that is not one. These bytes are a
    // .lnk header, which is exactly what Windows was left holding: QFile::link()
    // writes a shortcut there, wearing the .bdic name of the dictionary it was
    // meant to be. Nothing platform-specific is being asserted — a file that is
    // not a dictionary has to go, wherever it came from.
    QFile stale(user.filePath(QStringLiteral("stale.bdic")));
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write(QByteArray::fromHex("4c00000001140200"));
    stale.close();
#ifndef Q_OS_WIN
    // And where symlinks are real, one whose target is gone.
    QFile::link(QStringLiteral("/nonexistent/gone.bdic"),
                user.filePath(QStringLiteral("gone.bdic")));
#endif

    Dictionaries::syncDictionaryDirs(user.path(), bundle.path());

    const QStringList have =
        QDir(user.path()).entryList({QStringLiteral("*.bdic")}, QDir::Files);
    QVERIFY(have.contains(QStringLiteral("en_US.bdic")));  // mirrored
    QVERIFY(have.contains(QStringLiteral("es_ES.bdic")));  // mirrored
    QVERIFY(have.contains(QStringLiteral("zz_ZZ.bdic")));  // user's own, kept
    QVERIFY(!have.contains(QStringLiteral("stale.bdic"))); // pruned
    // Gone from disk, not merely missing from a listing that resolves symlinks.
    // That distinction is the whole bug: the shortcut was a plain file, so it
    // was listed, and only a check that reads it can tell it apart.
    QVERIFY(!QFileInfo::exists(user.filePath(QStringLiteral("stale.bdic"))));
#ifndef Q_OS_WIN
    QVERIFY(!have.contains(QStringLiteral("gone.bdic")));
#endif
  }
  // The rows of the language list (#46), which is the whole of its interface: what
  // each state offers, and in what order they are shown.
  void rowsSayWhatEachLanguageOffers() {
    const auto entry = [](const char *code, qint64 size, const char *sha) {
      DictionaryEntry e;
      e.code = QString::fromLatin1(code);
      e.size = size;
      e.sha256 = QString::fromLatin1(sha);
      return e;
    };
    const QList<DictionaryEntry> catalog{
        entry("en_US", 545259, "aa"), entry("pt_PT", 900000, "bb"),
        entry("da_DK", 838860, "cc"), entry("vi_VN", 100, "")};
    // en_US is bundled (mirrored from it, so not removable), pt_PT and eo were
    // downloaded, and eo is not in the catalogue at all.
    const QStringList installed{QStringLiteral("en_US"), QStringLiteral("pt_PT"),
                                QStringLiteral("eo")};
    const QStringList removable{QStringLiteral("pt_PT"), QStringLiteral("eo")};
    const QList<DictionaryRows::Row> rows =
        DictionaryRows::build(installed, removable, catalog);
    QCOMPARE(rows.size(), 5); // four catalogued, plus the un-catalogued eo

    const auto row = [&rows](const char *code) {
      for (const DictionaryRows::Row &r : rows)
        if (r.code == QLatin1String(code))
          return r;
      return DictionaryRows::Row{};
    };
    // Here and bundled: it can be ticked, and there is nothing to delete — it is
    // mirrored from the read-only bundle and would only come back next launch.
    QVERIFY(row("en_US").installed);
    QVERIFY(row("en_US").action == DictionaryRows::Action::None);
    // Here because it was downloaded: ticked, and it can go again.
    QVERIFY(row("pt_PT").action == DictionaryRows::Action::Delete);
    // Not here: the arrow, and the size it will cost.
    QVERIFY(!row("da_DK").installed);
    QVERIFY(row("da_DK").action == DictionaryRows::Action::Download);
    QCOMPARE(row("da_DK").downloadSize, Q_INT64_C(838860));
    // No sha256 in the manifest: not offered at all, because a .bdic that cannot be
    // verified must never reach Chromium.
    QVERIFY(row("vi_VN").action == DictionaryRows::Action::None);
    // On disk but unknown to the catalogue (a user's own file): still a row, still
    // deletable, and no download size to promise.
    QVERIFY(row("eo").installed);
    QVERIFY(row("eo").action == DictionaryRows::Action::Delete);
    QCOMPARE(row("eo").downloadSize, Q_INT64_C(0));

    // Ordered by the name each row shows, not by the code behind it.
    QStringList shown;
    for (const DictionaryRows::Row &r : rows)
      shown << r.label;
    QStringList sorted = shown;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
      return a.localeAwareCompare(b) < 0;
    });
    QCOMPARE(shown, sorted);

    // The Portuguese pair is the reason the label strips CLDR's qualifier: this list
    // says the territory itself, so "português europeu (Portugal)" said it twice.
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("pt_PT")),
             QStringLiteral("português (Portugal)"));
    QCOMPARE(Dictionaries::languageLabel(QStringLiteral("pt_BR")),
             QStringLiteral("português (Brasil)"));
  }
  // What hovering a row says: read off the file, with nothing invented.
  void rowTooltipTellsWhatIsKnown() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("eo.bdic")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1536 * 1024, 'x')); // 1.5 MiB, to be said in MB
    f.close();
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dir.path().toLocal8Bit());

    DictionaryRows::Row here;
    here.code = QStringLiteral("eo");
    here.installed = true;
    here.action = DictionaryRows::Action::Delete;
    const QString tip = DictionaryRows::tooltip(here);
    QVERIFY(tip.contains(QStringLiteral("1.5 MB")));
    QVERIFY(tip.endsWith(QStringLiteral("eo"))); // the code Chromium is given
    // No version and no upstream date: the manifest carries code, size and sha256
    // only, so either would be invented.
    QVERIFY(!tip.contains(QStringLiteral("version"), Qt::CaseInsensitive));

    DictionaryRows::Row absent;
    absent.code = QStringLiteral("da_DK");
    absent.downloadSize = 838860;
    absent.action = DictionaryRows::Action::Download;
    QVERIFY(DictionaryRows::tooltip(absent).contains(QStringLiteral("0.8 MB")));

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
  // The sender presses WhatsApp's own Send button, so it has to be able to find
  // it. WhatsApp renamed that icon to "wds-ic-send-filled" and no longer puts
  // "send" on the page at all; with only the old name the lookup fell through to
  // two hardcoded aria-labels, English and Spanish, and in any other interface
  // language the job failed on its own 45-second deadline reporting a timeout
  // waiting for the chat to open.
  void senderFindsTheCurrentSendIcon() {
    const QString js = ScheduledMessages::senderScriptSource();
    // The selectors themselves, not merely the icon name: the name also appears
    // in the comment above the lookup, so searching for it alone would still
    // pass with the selector deleted.
    const int footerFirst = js.indexOf(QLatin1String(
        "querySelector('footer [data-icon=\"wds-ic-send-filled\"]')"));
    const int anywhere = js.indexOf(QLatin1String(
        "querySelector('[data-icon=\"wds-ic-send-filled\"]')"));
    const int legacy = js.indexOf(QLatin1String("[data-icon=\"send\"]"));
    const int english = js.indexOf(QLatin1String("aria-label=\"Send\""));
    const int spanish = js.indexOf(QLatin1String("aria-label=\"Enviar\""));
    QVERIFY(footerFirst >= 0);
    QVERIFY(anywhere >= 0);
    QVERIFY(legacy >= 0); // the older name stays behind it, as a fallback
    QVERIFY(english >= 0);
    QVERIFY(spanish >= 0);
    // The order is the whole point: ask the current name before the older one
    // and before the two hardcoded labels, or the label path goes on carrying
    // the feature and it goes on failing outside English and Spanish.
    QVERIFY(footerFirst < legacy);
    QVERIFY(anywhere < legacy);
    QVERIFY(anywhere < english);
    QVERIFY(anywhere < spanish);
  }
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
    // #35: the recolouring must also handle WhatsApp's bare "r, g, b" channel
    // tokens (--*-RGB), re-emitted as channels via hslToRgb, not only full
    // colour values, or the message-options fade stays the unthemed green.
    ChatTheme::setCurrentThemeId(QStringLiteral("barbie"));
    const QString themed = ChatTheme::scriptSource();
    QVERIFY(themed.contains(QLatin1String("hslToRgb")));
    QVERIFY(themed.contains(QLatin1String("channels")));
    ChatTheme::setCurrentThemeId(QStringLiteral("none"));
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

  // #6: the expression-panel dismiss guard must recognise the skin-tone /
  // variant popover (separate popover, <img class="emojik">), so a click there
  // no longer closes the emoji panel mid-selection.
  void webTweaksEmojiVariantGuard() {
    const QString src = WebTweaks::scriptSource();
    QVERIFY(src.contains(QLatin1String(".emojik")));
    QVERIFY(src.contains(QLatin1String("skin-tone")));
    QVERIFY(src.contains(QLatin1String("emoji-variant")));
  }

  // The rail buttons' retry must be able to tell "still in the rail" from "still
  // in the document". Asking only the latter made a stranded entry permanent:
  // it is the stop condition for the retry timer, so once satisfied the buttons
  // were never rebuilt, and only destroying the page brought them back. Confirmed
  // live before the fix, so this guards the shape of the condition.
  void webTweaksRailButtonsRecoverFromReparenting() {
    const QString src = WebTweaks::scriptSource();
    // The remembered container, and the two comparisons against it.
    QVERIFY(src.contains(QLatin1String("railParent")));
    QVERIFY(src.contains(QLatin1String("entry.parentElement === railParent")));
    QVERIFY(src.contains(QLatin1String("existing.parentElement === railParent")));
    // A stranded entry must be removed rather than skipped, or its id stays taken
    // and no replacement can be built.
    QVERIFY(src.contains(QLatin1String("if (existing) existing.remove();")));
    // Still nothing on the timer path that forces layout.
    const int settledAt = src.indexOf(QLatin1String("var settled = function"));
    const int installAt = src.indexOf(QLatin1String("var install = function"));
    QVERIFY(settledAt > 0 && installAt > settledAt);
    QVERIFY(!src.mid(settledAt, installAt - settledAt)
                 .contains(QLatin1String("getBoundingClientRect")));
  }

  // #7: the always-on HD receive flag script overrides wa_web_show_hd_photo.
  void webTweaksHdFlagScript() {
    const QString hd = WebTweaks::hdFlagScriptSource();
    QVERIFY(!hd.isEmpty());
    QVERIFY(hd.contains(QLatin1String("wa_web_show_hd_photo")));
    QVERIFY(hd.contains(QLatin1String("WAWebABProps")));
  }

  // The chat-list strip is a toggle, so both directions have to produce a valid
  // script: collapsed installs the stylesheet, expanded removes it again. The
  // expanded script is NOT empty — it is what takes the stylesheet back out.
  void chatListStrip() {
    ChatListStrip::setCollapsed(false);
    QVERIFY(!ChatListStrip::isCollapsed());
    const QString off = ChatListStrip::scriptSource();
    QVERIFY(!off.isEmpty());
    // It still NAMES #pane-side — that is the tooltip cleanup — but it must
    // carry no width rule, or it would collapse the list while "expanded".
    QVERIFY(!off.contains(QLatin1String("min-width")));

    ChatListStrip::setCollapsed(true);
    QVERIFY(ChatListStrip::isCollapsed());
    const QString on = ChatListStrip::scriptSource();
    QVERIFY(on.contains(QLatin1String("#pane-side")));
    QVERIFY(on.contains(QLatin1String("min-width")));
    QVERIFY(on.contains(QLatin1String("whatly-chatlist-strip")));
    // The pane columns are found by measurement and tagged, because the layout
    // carries more than one of them and only styling the visible one leaves the
    // conversation's divider stranded at the old width.
    QVERIFY(on.contains(QLatin1String("data-whatly-pane")));
    // …and the width must never be set through the `flex` shorthand: in the
    // Calls section the column's parent runs vertically, so that would set the
    // height and fold the section into a 97px box.
    QVERIFY(!on.contains(QLatin1String("flex:0 0")));
    QVERIFY(on.contains(QLatin1String("flex-basis:auto")));
    // Collapsed, the clipped name/preview/time stay reachable as a hover
    // preview — a clone of the row, so emoji and formatting survive — and the
    // clipped search box stays reachable with one click.
    QVERIFY(on.contains(QLatin1String("pointerover")));
    QVERIFY(on.contains(QLatin1String("cloneNode")));
    QVERIFY(on.contains(QLatin1String("whatly-chatlist-tip")));

    ChatListStrip::setCollapsed(false);
  }

  // Two things withdraw the strip without touching the setting: leaving Chats,
  // and a filter that empties the list — Favourites with no favourites, where
  // WhatsApp swaps the rows for an explanatory panel that at 97px is unreadable
  // fragments. Both are decided live in the page, so what a test can pin is
  // that the script still carries both guards and still asks for both.
  void chatListStripStandsDownWithNothingToShow() {
    ChatListStrip::setCollapsed(true);
    const QString on = ChatListStrip::scriptSource();
    QVERIFY(on.contains(QLatin1String("aria-pressed")));   // which section
    QVERIFY(on.contains(QLatin1String("var listed")));     // are there rows
    QVERIFY(on.contains(QLatin1String("inChats() && listed()")));
    ChatListStrip::setCollapsed(false);
  }

  // WhatsApp's own notices in the list — the "Refresh to update" banner is the
  // one seen in the wild — are not rows, so nothing that keeps a row to its
  // avatar reaches them: a row's name and message sit on one line and are cut
  // off at 97px, while a notice's sentence wraps, and the column turns it into a
  // ladder of single letters the height of the strip. They are handled as the
  // topmost chat instead.
  //
  // Nothing in this repo drives a real page, so what a test here can pin is that
  // the script still carries every half of the mechanism. The behaviour is
  // driven against a stand-in page built from the markup as it was read off the
  // live banner, and the assertions below are chosen to fail on the two ways
  // that markup has already caught this code out: looking for the notice INSIDE
  // the list, when it sits above it, and matching only block-level tags, when it
  // is a span. A detector that matches nothing at all still reads as a working
  // one from in here, which is exactly why those two are pinned negatively.
  void chatListStripTamesTheUpdateNotice() {
    ChatListStrip::setCollapsed(true);
    const QString on = ChatListStrip::scriptSource();
    QVERIFY(on.contains(QLatin1String("data-whatly-banner")));
    // Clipped like a row rather than wrapped: the one rule that does the work.
    // The notice's own box is in the selector because its sentences sit in it as
    // bare text, which `*` cannot reach.
    QVERIFY(on.contains(QLatin1String(
        "[data-whatly-banner],[data-whatly-banner] *{white-space:nowrap")));
    // Matched by the name WhatsApp chose for the bar, not by the generated
    // class names next to it on the same element.
    QVERIFY(on.contains(QLatin1String("[data-testid=\"chat-butterbar\"]")));
    // …and never by searching the list itself: the notice is the previous
    // sibling of #pane-side, so a search scoped inside it finds nothing at all.
    QVERIFY(!on.contains(QLatin1String("listPane")));
    QVERIFY(on.contains(QLatin1String("previousElementSibling")));
    // The bar stays in the page while empty once the update is applied, so the
    // content tests are what stop the strip drawing a permanent phantom cell.
    QVERIFY(on.contains(QLatin1String("svg,[data-icon]")));
    // The preview is where the message is actually read, so the clone must not
    // inherit the one-line clipping the strip imposes.
    QVERIFY(on.contains(
        QLatin1String("clone.removeAttribute('data-whatly-banner')")));
    // Collapsed, the notice's own button is one of the things clipped away, so
    // a click on the icon has to be carried to it — and stopped hard, or the
    // handler that opens the search box takes the same click and uncollapses.
    QVERIFY(on.contains(QLatin1String("act.click()")));
    QVERIFY(on.contains(QLatin1String("ev.stopImmediatePropagation()")));
    // …and that carried click arrives as a fresh event on the button, so the
    // search handler has to let the notice's own controls past. Without this it
    // cancels the click in the capture phase and opens the search box instead,
    // which is the whole of what "the button does nothing" looked like.
    QVERIFY(on.contains(QLatin1String("[data-whatly-banner] button,")));

    // Expanded, the clipping rule must be gone with the rest of the stylesheet,
    // or a notice would be held to one line in a list that is not collapsed.
    ChatListStrip::setCollapsed(false);
    const QString off = ChatListStrip::scriptSource();
    QVERIFY(!off.contains(
        QLatin1String("[data-whatly-banner] *{white-space:nowrap")));
  }

  // The hover preview's default size follows the platform: the value settled on
  // against Windows' font rendering came back too small from Linux. Whatever
  // the default, the chosen id has to reach the script as a NUMBER, and an id
  // that is not one of ours must fall back rather than put "undefined" into the
  // page, where it would take the whole preview down with it.
  void chatListStripPreviewSize() {
    // Ask what a FRESH INSTALL defaults to, which means asking with nothing
    // stored. Reading the current id instead would return whatever a previous
    // run left behind — the settings store outlives the process, and
    // tst_settings builds a real SettingsWidget after this suite — and the
    // fallback assertion below would then be comparing against that rather
    // than against the default it is meant to check.
    QSettings &settings = SettingsManager::instance().settings();
    settings.remove(QStringLiteral("chatListStripPreviewSize"));
    const QString freshDefault = ChatListStrip::currentPreviewSizeId();
    bool known = false;
    for (const ChatListStrip::PreviewSize &size : ChatListStrip::previewSizes())
      if (size.id == freshDefault)
        known = true;
    QVERIFY(known);

    ChatListStrip::setCollapsed(true);
    ChatListStrip::setCurrentPreviewSizeId(QStringLiteral("large"));
    QCOMPARE(ChatListStrip::currentPreviewSizeId(), QStringLiteral("large"));
    QVERIFY(ChatListStrip::scriptSource().contains(
        QLatin1String("__whatlyStripZoom = 1;")));
    ChatListStrip::setCurrentPreviewSizeId(QStringLiteral("small"));
    const QString source = ChatListStrip::scriptSource();
    QVERIFY(source.contains(QLatin1String("__whatlyStripZoom = 0.7;")));

    // The preview outlives the run that defined it, so the size has to reach it
    // through the window rather than through a captured variable, and it has to
    // be set BEFORE the once-per-page guard — everything past that guard is
    // skipped on a re-run, which is exactly what changing the setting does.
    QVERIFY(source.contains(QLatin1String("window.__whatlyStripZoom ||")));
    const int zoomAt = source.indexOf(QLatin1String("__whatlyStripZoom ="));
    const int guardAt =
        source.indexOf(QLatin1String("__whatlyStripReady) return;"));
    QVERIFY(zoomAt >= 0);
    QVERIFY(guardAt >= 0);
    QVERIFY(zoomAt < guardAt);

    ChatListStrip::setCurrentPreviewSizeId(QStringLiteral("nonsense"));
    QCOMPARE(ChatListStrip::currentPreviewSizeId(), freshDefault);
    QVERIFY(!ChatListStrip::scriptSource().contains(
        QLatin1String("__whatlyStripZoom = undefined")));

    // Leave the setting UNSET rather than pinned to the default, so the next
    // run starts from the same place this one did.
    settings.remove(QStringLiteral("chatListStripPreviewSize"));
    ChatListStrip::setCollapsed(false);
  }

  // The rail button reads its own state off that stylesheet's id, so the two
  // modules have to agree on it — nothing else connects them.
  void chatListStripButtonMatchesStylesheetId() {
    const QString tweaks = WebTweaks::scriptSource();
    QVERIFY(tweaks.contains(QLatin1String("whatly-chatlist-strip")));
    QVERIFY(tweaks.contains(QLatin1String("toggleChatListStrip")));
    ChatListStrip::setCollapsed(true);
    QVERIFY(ChatListStrip::scriptSource().contains(
        QLatin1String("whatly-chatlist-strip")));
    ChatListStrip::setCollapsed(false);
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

  // #5: the notification popup anchors to the top-right of the screen's
  // available area with a margin, fully on-screen — including on a secondary
  // monitor whose geometry does not start at (0,0).
  void notificationPopupAnchor() {
    // Primary screen at the origin.
    const QPoint p =
        Utils::topRightWithin(QRect(0, 0, 1920, 1080), QSize(300, 100), 20);
    QCOMPARE(p, QPoint(1920 - 300 - 20, 20)); // 1600, 20

    // Secondary monitor offset to the right and down: the anchor must follow
    // the monitor's own origin, not assume (0,0) (the bug this fixed).
    const QPoint s =
        Utils::topRightWithin(QRect(1920, 200, 1280, 1024), QSize(300, 100), 20);
    QCOMPARE(s, QPoint(1920 + 1280 - 300 - 20, 200 + 20)); // 2880, 220
  }

  // #8: the tray-click "was frontmost a moment ago" heuristic. The grace window
  // recovers a window that lost activation to the shell just before the click
  // (Windows); graceMs <= 0 keeps every other platform on true activation only.
  void trayFrontmostGrace() {
    // Active now is always frontmost, regardless of grace.
    QVERIFY(Utils::wasFrontmostRecently(true, 0, 100000, 0));
    QVERIFY(Utils::wasFrontmostRecently(true, 0, 100000, 300));

    // Not active, with a grace window: frontmost only if it deactivated within it.
    QVERIFY(Utils::wasFrontmostRecently(false, 100000, 100200, 300));  // 200ms ago
    QVERIFY(!Utils::wasFrontmostRecently(false, 100000, 100500, 300)); // 500ms ago

    // Grace disabled (non-Windows): an inactive window is never frontmost, so
    // the behaviour is exactly the pre-change isActiveWindow() check.
    QVERIFY(!Utils::wasFrontmostRecently(false, 100000, 100010, 0));
  }

  // The order the tray offers the windows in, and the order they are brought back
  // in. Strings stand in for windows: the rule is about the two lists, not about
  // anything a window does.
  void windowsOrderedByUse() {
    using L = QList<QString>;
    const L all{"main", "a", "b", "c"};

    // Most-recently-used first, and the history repeats itself — every window is
    // re-recorded each time it is activated, so "b, a, b" is the normal shape.
    QCOMPARE(Utils::orderedByHistory(L{"b", "a", "b"}, all),
             (L{"b", "a", "main", "c"}));

    // A window never activated is still offered, on the end: leaving it out is
    // how a window ends up with nothing pointing at it, which is the whole bug.
    QCOMPARE(Utils::orderedByHistory(L{"c"}, all), (L{"c", "main", "a", "b"}));

    // A history naming a window that has since closed does not resurrect it.
    QCOMPARE(Utils::orderedByHistory(L{"gone", "a"}, all),
             (L{"a", "main", "b", "c"}));

    // No history at all: the list as it stands, which is what a fresh start has.
    QCOMPARE(Utils::orderedByHistory(L{}, all), all);
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

  // The call popout (web.whatsapp.com/call/popout) and any other WhatsApp-origin
  // window.open must be kept in-app; everything else is an external link.
  void inAppPopupUrl() {
    QVERIFY(isInAppPopupUrl(
        QUrl(QStringLiteral("https://web.whatsapp.com/call/popout"))));
    QVERIFY(isInAppPopupUrl(QUrl(QStringLiteral("https://web.whatsapp.com/"))));
    QVERIFY(!isInAppPopupUrl(QUrl(QStringLiteral("https://example.com/x"))));
    QVERIFY(!isInAppPopupUrl(
        QUrl(QStringLiteral("https://faq.whatsapp.com/help")))); // other host
    QVERIFY(!isInAppPopupUrl(QUrl(QStringLiteral("about:blank")))); // no host
    QVERIFY(!isInAppPopupUrl(QUrl()));                             // invalid
  }

  // The account tab tooltip: version line and build-token line, each dropped
  // when empty, joined by a newline.
  void accountTooltip() {
    QCOMPARE(accountTabTooltipText(QStringLiteral("2.3000.104"),
                                   QStringLiteral("147.977")),
             QStringLiteral("WhatsApp Web 2.3000.104\nBuild token: 147.977"));
    // Version not yet known: only the token line.
    QCOMPARE(accountTabTooltipText(QString(), QStringLiteral("147.977")),
             QStringLiteral("Build token: 147.977"));
    // Neither available: empty tooltip, no stray newline.
    QCOMPARE(accountTabTooltipText(QString(), QString()), QString());
    QVERIFY(!accountTabTooltipText(QStringLiteral("2.3000.104"), QString())
                 .contains(QLatin1Char('\n')));
  }

  // The tray icon's tooltip, which is where the whole count lives: the badge can
  // only show one number, and past ninety-nine not even that.
  void trayTooltip() {
    QCOMPARE(trayTooltipText(UnreadBreakdown{}),
             QStringLiteral("Whatly\nNothing unread"));

    // No muted split available (the drawn-rows fallback cannot see it): say what is
    // known and nothing more. Reporting "0 muted" would be a claim, not a silence.
    UnreadBreakdown plain;
    plain.chats = 3;
    plain.messages = 7;
    QCOMPARE(trayTooltipText(plain),
             QStringLiteral("Whatly\n7 unread messages in 3 chats"));

    // The split, which is the point of asking for a tooltip at all.
    UnreadBreakdown split;
    split.chats = 12;
    split.messages = 47;
    split.mutedChats = 3;
    split.mutedMessages = 9;
    split.mutedKnown = true;
    const QStringList lines =
        trayTooltipText(split).split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(1), QStringLiteral("47 unread messages in 12 chats"));
    QCOMPARE(lines.at(2), QStringLiteral("9 in 3 muted chats"));
    QCOMPARE(lines.at(3), QStringLiteral("38 in 9 chats that are not muted"));

    // The badge set NOT to count muted: chats/messages then exclude the muted
    // ones, which are a disjoint set. The tooltip must add them back for the real
    // total and split, instead of subtracting a disjoint set into a negative
    // "-7 in 1 chat that is not muted" (the bug this pins).
    UnreadBreakdown offBadge;
    offBadge.chats = 5;     // non-muted only
    offBadge.messages = 10; // non-muted only
    offBadge.mutedChats = 3;
    offBadge.mutedMessages = 9;
    offBadge.mutedKnown = true;
    offBadge.mutedInTotal = false;
    const QStringList off = trayTooltipText(offBadge).split(QLatin1Char('\n'));
    QCOMPARE(off.size(), 4);
    QCOMPARE(off.at(1), QStringLiteral("19 unread messages in 8 chats"));
    QCOMPARE(off.at(2), QStringLiteral("9 in 3 muted chats"));
    QCOMPARE(off.at(3), QStringLiteral("10 in 5 chats that are not muted"));

    // Everything muted: no line about chats that are not, since there are none.
    UnreadBreakdown allMuted;
    allMuted.chats = 2;
    allMuted.messages = 5;
    allMuted.mutedChats = 2;
    allMuted.mutedMessages = 5;
    allMuted.mutedKnown = true;
    QCOMPARE(trayTooltipText(allMuted).split(QLatin1Char('\n')).size(), 3);
    QVERIFY(!trayTooltipText(allMuted).contains(QStringLiteral("not muted")));

    // Nothing muted, and known so: also no split, rather than "0 in 0 muted".
    UnreadBreakdown noneMuted;
    noneMuted.chats = 4;
    noneMuted.messages = 4;
    noneMuted.mutedKnown = true;
    QCOMPARE(trayTooltipText(noneMuted).split(QLatin1Char('\n')).size(), 2);

    // One of each reads as one of each.
    UnreadBreakdown one;
    one.chats = 1;
    one.messages = 1;
    QCOMPARE(trayTooltipText(one),
             QStringLiteral("Whatly\n1 unread message in 1 chat"));
  }

  // Group-invite links resolve to their code; sends and other URLs do not.
  void inviteCode() {
    QCOMPARE(inviteCodeFromUrl(QStringLiteral(
                 "https://chat.whatsapp.com/Ia4u47cGotk6AIAc6S0P36")),
             QStringLiteral("Ia4u47cGotk6AIAc6S0P36"));
    QCOMPARE(inviteCodeFromUrl(QStringLiteral("chat.whatsapp.com/XyZ_123-4")),
             QStringLiteral("XyZ_123-4"));
    QCOMPARE(inviteCodeFromUrl(QStringLiteral("whatsapp://chat?code=ABC123")),
             QStringLiteral("ABC123"));
    QCOMPARE(inviteCodeFromUrl(QStringLiteral("whatsapp://chat/?code=ABC123")),
             QStringLiteral("ABC123"));
    // A send request is not an invite, even if its text mentions "code=".
    QCOMPARE(inviteCodeFromUrl(QStringLiteral(
                 "whatsapp://send?phone=34600000000&text=my%20code=hi")),
             QString());
    QCOMPARE(inviteCodeFromUrl(QStringLiteral("https://example.com/x")),
             QString());
    QCOMPARE(inviteCodeFromUrl(QString()), QString());
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

  // The file-management half of captureNativeStderr() (issue #3): each session
  // starts a fresh capture file and keeps the previous one as "<name>.prev", so
  // a crash log survives one relaunch. Tested without the fd-2 redirect, which
  // would hijack the test runner's own stderr.
  void rotateCaptureKeepsPrevious() {
    const QString path =
        QDir::tempPath() + QStringLiteral("/whatly_capture_test.log");
    const QString prev = path + QStringLiteral(".prev");
    QFile::remove(path);
    QFile::remove(prev);

    // First session: creates a fresh, empty file; no .prev yet.
    QVERIFY(DebugLog::rotateCaptureFile(path));
    QVERIFY(QFile::exists(path));
    QVERIFY(!QFile::exists(prev));

    // Simulate a crash having written to it.
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("[FATAL] Check failed: previous session\n");
      f.close();
    }

    // Second session: the crash log is preserved as .prev and a fresh, empty
    // file is left in place.
    QVERIFY(DebugLog::rotateCaptureFile(path));
    QVERIFY(QFile::exists(prev));
    QVERIFY(QFile::exists(path));
    QCOMPARE(QFileInfo(path).size(), qint64(0));
    {
      QFile f(prev);
      QVERIFY(f.open(QIODevice::ReadOnly));
      QVERIFY(f.readAll().contains("previous session"));
      f.close();
    }

    QFile::remove(path);
    QFile::remove(prev);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Messaging core: recipient parsing, template filling and backend mapping for
// the "send by command / API" feature. All pure functions.
class TstMessaging : public QObject {
  Q_OBJECT
private slots:
  void parsesPhoneNumbers() {
    auto r = Messaging::parseRecipient(QStringLiteral("+34 600-123-456"));
    QCOMPARE(r.kind, Messaging::RecipientKind::PhoneNumber);
    QCOMPARE(r.value, QStringLiteral("34600123456"));
    r = Messaging::parseRecipient(QStringLiteral("(555) 123.4567"));
    QCOMPARE(r.kind, Messaging::RecipientKind::PhoneNumber);
    QCOMPARE(r.value, QStringLiteral("5551234567"));
  }

  void parsesGroups() {
    auto r = Messaging::parseRecipient(QStringLiteral("120363012345678901@g.us"));
    QCOMPARE(r.kind, Messaging::RecipientKind::GroupId);
    QCOMPARE(r.value, QStringLiteral("120363012345678901"));
    r = Messaging::parseRecipient(QStringLiteral("group:120363012345678901"));
    QCOMPARE(r.kind, Messaging::RecipientKind::GroupId);
    QCOMPARE(r.value, QStringLiteral("120363012345678901"));
    // A legacy group id keeps its '-' separator (it is part of the id, unlike a
    // phone number's punctuation).
    r = Messaging::parseRecipient(QStringLiteral("group:11111111111-2222222222"));
    QCOMPARE(r.kind, Messaging::RecipientKind::GroupId);
    QCOMPARE(r.value, QStringLiteral("11111111111-2222222222"));
    r = Messaging::parseRecipient(QStringLiteral("11111111111-2222222222@g.us"));
    QCOMPARE(r.value, QStringLiteral("11111111111-2222222222"));
  }

  void parsesContactNames() {
    auto r = Messaging::parseRecipient(QStringLiteral("Alice Smith"));
    QCOMPARE(r.kind, Messaging::RecipientKind::ContactName);
    QCOMPARE(r.value, QStringLiteral("Alice Smith"));
    // "name:" forces a name even for something that looks like a number.
    r = Messaging::parseRecipient(QStringLiteral("name:12345"));
    QCOMPARE(r.kind, Messaging::RecipientKind::ContactName);
    QCOMPARE(r.value, QStringLiteral("12345"));
  }

  void invalidRecipient() {
    QCOMPARE(Messaging::parseRecipient(QStringLiteral("   ")).kind,
             Messaging::RecipientKind::Invalid);
  }

  void templatePlaceholdersAndFill() {
    const QString body =
        QStringLiteral("Hi {{ name }}, your code is {{code}}. Bye {{name}}.");
    QCOMPARE(Messaging::templatePlaceholders(body),
             (QStringList{QStringLiteral("name"), QStringLiteral("code")}));
    QMap<QString, QString> vars;
    vars.insert(QStringLiteral("name"), QStringLiteral("Ada"));
    vars.insert(QStringLiteral("code"), QStringLiteral("42"));
    QCOMPARE(Messaging::fillTemplate(body, vars),
             QStringLiteral("Hi Ada, your code is 42. Bye Ada."));
    // An unknown placeholder is left untouched, so it is visibly unfilled.
    QMap<QString, QString> partial;
    partial.insert(QStringLiteral("name"), QStringLiteral("Ada"));
    QCOMPARE(Messaging::fillTemplate(QStringLiteral("{{name}}/{{code}}"), partial),
             QStringLiteral("Ada/{{code}}"));
  }

  void parseVarsSplitsOnFirstEquals() {
    const auto m = Messaging::parseVars(
        {QStringLiteral("a=1"), QStringLiteral("url=http://x?y=z"),
         QStringLiteral("noequals"), QStringLiteral("=bad")});
    QCOMPARE(m.value(QStringLiteral("a")), QStringLiteral("1"));
    QCOMPARE(m.value(QStringLiteral("url")), QStringLiteral("http://x?y=z"));
    QVERIFY(!m.contains(QStringLiteral("noequals")));
    QCOMPARE(m.size(), 2);
  }

  void backendMapping() {
    bool ok = false;
    QCOMPARE(Messaging::parseBackend(QStringLiteral("Web"), &ok),
             Messaging::Backend::Web);
    QVERIFY(ok);
    QCOMPARE(Messaging::parseBackend(QStringLiteral("cloud"), &ok),
             Messaging::Backend::Cloud);
    QVERIFY(ok);
    Messaging::parseBackend(QStringLiteral("carrier-pigeon"), &ok);
    QVERIFY(!ok);
    QCOMPARE(Messaging::backendName(Messaging::Backend::Cloud),
             QStringLiteral("cloud"));
  }

  // The IPC payload survives a round-trip with spaces, newlines and unicode in
  // the message (which a space-joined argv would mangle), and a non-send
  // payload is rejected so the receiver falls through to its other handling.
  void sendCommandRoundTrip() {
    Messaging::SendCommand cmd;
    cmd.backend = Messaging::Backend::Cloud;
    cmd.to = QStringLiteral("+34 600 123 456");
    cmd.message = QStringLiteral("Hola\nmundo — ¿qué tal? 😀 a=b c=d");
    cmd.file = QStringLiteral("/home/u/My Photos/año 2026.jpg");
    const QString payload = Messaging::encodeSendCommand(cmd);

    Messaging::SendCommand got;
    QVERIFY(Messaging::decodeSendCommand(payload, &got));
    QCOMPARE(got.backend, Messaging::Backend::Cloud);
    QCOMPARE(got.to, cmd.to);
    QCOMPARE(got.message, cmd.message);
    QCOMPARE(got.file, cmd.file);

    // A plain CLI argv string is not a send command.
    QVERIFY(!Messaging::decodeSendCommand(QStringLiteral("-s --open-settings"),
                                          nullptr));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Whatly's own reusable message templates (per-account store). Runs in
// QStandardPaths test mode, so it writes to a throwaway settings file.
class TstMessageTemplates : public QObject {
  Q_OBJECT
private slots:
  void init() { MessageTemplates::setAll({}); } // clean slate each case

  void addUpdateRemove() {
    QVERIFY(!MessageTemplates::exists(QStringLiteral("welcome")));
    MessageTemplates::set(QStringLiteral("welcome"),
                          QStringLiteral("Hi {{name}}"));
    QVERIFY(MessageTemplates::exists(QStringLiteral("welcome")));
    QCOMPARE(MessageTemplates::body(QStringLiteral("welcome")),
             QStringLiteral("Hi {{name}}"));

    // set() on the same name updates the body rather than duplicating.
    MessageTemplates::set(QStringLiteral("welcome"),
                          QStringLiteral("Hello {{name}}!"));
    QCOMPARE(MessageTemplates::all().size(), 1);
    QCOMPARE(MessageTemplates::body(QStringLiteral("welcome")),
             QStringLiteral("Hello {{name}}!"));

    QVERIFY(MessageTemplates::remove(QStringLiteral("welcome")));
    QVERIFY(!MessageTemplates::exists(QStringLiteral("welcome")));
    QVERIFY(!MessageTemplates::remove(QStringLiteral("welcome"))); // already gone
  }

  void ignoresEmptyName() {
    MessageTemplates::set(QStringLiteral("   "), QStringLiteral("x"));
    QCOMPARE(MessageTemplates::all().size(), 0);
  }

  // The end-to-end resolution used by `--send --template`: look up the body,
  // then fill it with Messaging.
  void resolveAndFill() {
    MessageTemplates::set(QStringLiteral("otp"),
                          QStringLiteral("Your code is {{code}} ({{name}})"));
    const QString body = MessageTemplates::body(QStringLiteral("otp"));
    QMap<QString, QString> vars = Messaging::parseVars(
        {QStringLiteral("code=1234"), QStringLiteral("name=Ada")});
    QCOMPARE(Messaging::fillTemplate(body, vars),
             QStringLiteral("Your code is 1234 (Ada)"));
    // A missing var leaves its placeholder, which templatePlaceholders() surfaces.
    QCOMPARE(Messaging::templatePlaceholders(
                 Messaging::fillTemplate(body, {{QStringLiteral("code"),
                                                 QStringLiteral("9")}})),
             QStringList{QStringLiteral("name")});
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Auto-reply ("listener") rules engine: matching incoming text and producing
// the reply. All pure.
class TstAutoReply : public QObject {
  Q_OBJECT
  using R = AutoReply::Rule;
  using MT = AutoReply::MatchType;
private slots:
  void init() {
    AutoReply::setEnabled(false);
    AutoReply::setStoredRules({});
    AutoReply::setRulesFilePath(QString());
  }

  void exactMatch() {
    R r; r.type = MT::Exact; r.pattern = QStringLiteral("ping"); r.reply = QStringLiteral("pong");
    QVERIFY(AutoReply::matches(r, QStringLiteral("  ping  ")));   // trimmed
    QVERIFY(AutoReply::matches(r, QStringLiteral("PING")));       // case-insensitive default
    QVERIFY(!AutoReply::matches(r, QStringLiteral("ping me")));
    r.caseSensitive = true;
    QVERIFY(!AutoReply::matches(r, QStringLiteral("PING")));
  }

  void containsMatch() {
    R r; r.type = MT::Contains; r.pattern = QStringLiteral("precio");
    QVERIFY(AutoReply::matches(r, QStringLiteral("¿cuál es el PRECIO?")));
    QVERIFY(!AutoReply::matches(r, QStringLiteral("gratis")));
  }

  void hashtagMatch() {
    R r; r.type = MT::Hashtag; r.pattern = QStringLiteral("oferta");
    QVERIFY(AutoReply::matches(r, QStringLiteral("gran #oferta hoy")));
    QVERIFY(AutoReply::matches(r, QStringLiteral("#OFERTA")));      // case-insensitive
    QVERIFY(!AutoReply::matches(r, QStringLiteral("una oferta")));  // needs the #
    QVERIFY(!AutoReply::matches(r, QStringLiteral("#ofertas")));    // whole token
    // A leading '#' in the pattern is tolerated.
    r.pattern = QStringLiteral("#oferta");
    QVERIFY(AutoReply::matches(r, QStringLiteral("mira #oferta")));
  }

  void regexWithCaptures() {
    R r; r.type = MT::Regex;
    r.pattern = QStringLiteral("pedido (\\d+)");
    r.reply = QStringLiteral("Tu pedido $1 está en camino");
    QStringList caps;
    QVERIFY(AutoReply::matches(r, QStringLiteral("estado del pedido 42?"), &caps));
    QCOMPARE(caps.value(1), QStringLiteral("42"));
    QList<R> rules{r};
    QCOMPARE(AutoReply::evaluate(QStringLiteral("estado del pedido 42?"), rules),
             QStringLiteral("Tu pedido 42 está en camino"));
  }

  void firstEnabledWins() {
    R a; a.type = MT::Contains; a.pattern = QStringLiteral("hola"); a.reply = QStringLiteral("A"); a.enabled = false;
    R b; b.type = MT::Contains; b.pattern = QStringLiteral("hola"); b.reply = QStringLiteral("B");
    QCOMPARE(AutoReply::evaluate(QStringLiteral("hola!"), {a, b}), QStringLiteral("B"));
    QCOMPARE(AutoReply::evaluate(QStringLiteral("nada"), {a, b}), QString());
  }

  void invalidRegexNoCrash() {
    R r; r.type = MT::Regex; r.pattern = QStringLiteral("([unclosed"); r.reply = QStringLiteral("x");
    QVERIFY(!AutoReply::matches(r, QStringLiteral("anything")));
  }

  void backendNameRoundTrip() {
    bool ok = false;
    QCOMPARE(AutoReply::parseMatchType(QStringLiteral("Regex"), &ok), MT::Regex);
    QVERIFY(ok);
    AutoReply::parseMatchType(QStringLiteral("nope"), &ok);
    QVERIFY(!ok);
    QCOMPARE(AutoReply::matchTypeName(MT::Hashtag), QStringLiteral("hashtag"));
  }

  void jsonRoundTrip() {
    R a; a.type = MT::Regex; a.pattern = QStringLiteral("pedido (\\d+)");
    a.reply = QStringLiteral("nº $1"); a.caseSensitive = true; a.enabled = false;
    R b; b.type = MT::Hashtag; b.pattern = QStringLiteral("oferta"); b.reply = QStringLiteral("¡Sí!");
    const QByteArray json = AutoReply::rulesToJson({a, b});
    QString err;
    const QList<R> back = AutoReply::rulesFromJson(json, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(back.size(), 2);
    QCOMPARE(back[0].type, MT::Regex);
    QCOMPARE(back[0].pattern, a.pattern);
    QCOMPARE(back[0].reply, a.reply);
    QVERIFY(back[0].caseSensitive);
    QVERIFY(!back[0].enabled);
    QCOMPARE(back[1].type, MT::Hashtag);
    // Malformed JSON reports an error and yields nothing (never throws).
    AutoReply::rulesFromJson(QByteArray("{ not json"), &err);
    QVERIFY(!err.isEmpty());
  }

  void storeAndActiveRules() {
    // replyFor is gated by the master switch.
    R s; s.type = MT::Contains; s.pattern = QStringLiteral("hola"); s.reply = QStringLiteral("stored");
    AutoReply::setStoredRules({s});
    QCOMPARE(AutoReply::replyFor(QStringLiteral("hola")), QString()); // disabled
    AutoReply::setEnabled(true);
    QCOMPARE(AutoReply::replyFor(QStringLiteral("hola")), QStringLiteral("stored"));

    // A rules file is merged after the stored rules and re-read each call.
    QTemporaryFile file;
    file.setFileTemplate(QDir::tempPath() + QStringLiteral("/whatly_rules_XXXXXX.json"));
    QVERIFY(file.open());
    R f; f.type = MT::Exact; f.pattern = QStringLiteral("ping"); f.reply = QStringLiteral("from-file");
    file.write(AutoReply::rulesToJson({f}));
    file.flush();
    AutoReply::setRulesFilePath(file.fileName());
    QCOMPARE(AutoReply::activeRules().size(), 2); // stored + file
    QCOMPARE(AutoReply::replyFor(QStringLiteral("ping")), QStringLiteral("from-file"));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cloud API (Meta WhatsApp Business) request builders — pure.
class TstCloudApi : public QObject {
  Q_OBJECT
  static QJsonObject obj(const QByteArray &j) { return QJsonDocument::fromJson(j).object(); }
private slots:
  void urls() {
    QCOMPARE(CloudApi::messagesUrl(QStringLiteral("v21.0"), QStringLiteral("123")),
             QStringLiteral("https://graph.facebook.com/v21.0/123/messages"));
    QCOMPARE(CloudApi::mediaUrl(QStringLiteral("v21.0"), QStringLiteral("123")),
             QStringLiteral("https://graph.facebook.com/v21.0/123/media"));
  }

  void textPayload() {
    auto o = obj(CloudApi::textPayload(QStringLiteral("+34 600-123-456"),
                                       QStringLiteral("Hola 😀")));
    QCOMPARE(o.value("messaging_product").toString(), QStringLiteral("whatsapp"));
    QCOMPARE(o.value("to").toString(), QStringLiteral("34600123456")); // digits only
    QCOMPARE(o.value("type").toString(), QStringLiteral("text"));
    QCOMPARE(o.value("text").toObject().value("body").toString(),
             QStringLiteral("Hola 😀"));
  }

  void mediaPayload() {
    auto o = obj(CloudApi::mediaPayload(QStringLiteral("34600123456"),
                                        QStringLiteral("image"),
                                        QStringLiteral("MID1"),
                                        QStringLiteral("una foto")));
    QCOMPARE(o.value("type").toString(), QStringLiteral("image"));
    auto img = o.value("image").toObject();
    QCOMPARE(img.value("id").toString(), QStringLiteral("MID1"));
    QCOMPARE(img.value("caption").toString(), QStringLiteral("una foto"));
    // Audio takes no caption.
    auto a = obj(CloudApi::mediaPayload(QStringLiteral("34600123456"),
                                        QStringLiteral("audio"),
                                        QStringLiteral("MID2"),
                                        QStringLiteral("ignored")));
    QVERIFY(!a.value("audio").toObject().contains(QStringLiteral("caption")));
  }

  void templatePayload() {
    auto o = obj(CloudApi::templatePayload(
        QStringLiteral("34600123456"), QStringLiteral("order_update"),
        QStringLiteral("es"), {QStringLiteral("42"), QStringLiteral("hoy")}));
    QCOMPARE(o.value("type").toString(), QStringLiteral("template"));
    auto t = o.value("template").toObject();
    QCOMPARE(t.value("name").toString(), QStringLiteral("order_update"));
    QCOMPARE(t.value("language").toObject().value("code").toString(),
             QStringLiteral("es"));
    auto comps = t.value("components").toArray();
    QCOMPARE(comps.size(), 1);
    auto params = comps.first().toObject().value("parameters").toArray();
    QCOMPARE(params.size(), 2);
    QCOMPARE(params.first().toObject().value("text").toString(),
             QStringLiteral("42"));
  }

  void mediaTypeMapping() {
    QCOMPARE(CloudApi::mediaTypeForMime(QStringLiteral("image/png")),
             QStringLiteral("image"));
    QCOMPARE(CloudApi::mediaTypeForMime(QStringLiteral("video/mp4")),
             QStringLiteral("video"));
    QCOMPARE(CloudApi::mediaTypeForMime(QStringLiteral("audio/ogg")),
             QStringLiteral("audio"));
    QCOMPARE(CloudApi::mediaTypeForMime(QStringLiteral("application/pdf")),
             QStringLiteral("document"));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Local HTTP API request parsing / auth / mapping — pure.
class TstLocalApi : public QObject {
  Q_OBJECT
private slots:
  void parsesRequestAndBody() {
    QByteArray raw =
        "POST /send HTTP/1.1\r\n"
        "Host: 127.0.0.1:8590\r\n"
        "Authorization: Bearer s3cr3t\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 12\r\n"
        "\r\n"
        "{\"to\":\"123\"}";
    auto r = LocalApi::parseRequest(raw);
    QVERIFY(r.headersComplete);
    QVERIFY(r.bodyComplete);
    QCOMPARE(r.method, QStringLiteral("POST"));
    QCOMPARE(r.path, QStringLiteral("/send"));
    QCOMPARE(r.headers.value("authorization"), QStringLiteral("Bearer s3cr3t"));
    QCOMPARE(r.body, QByteArray("{\"to\":\"123\"}"));
  }

  void partialBodyIsIncomplete() {
    QByteArray raw = "POST /send HTTP/1.1\r\n"
                     "Content-Length: 20\r\n\r\n"
                     "{\"to\":";
    auto r = LocalApi::parseRequest(raw);
    QVERIFY(r.headersComplete);
    QVERIFY(!r.bodyComplete); // fewer bytes than Content-Length
  }

  void authNeedsExactBearer() {
    QByteArray raw = "GET / HTTP/1.1\r\nAuthorization: Bearer good\r\n\r\n";
    auto r = LocalApi::parseRequest(raw);
    QVERIFY(LocalApi::authorized(r, QStringLiteral("good")));
    QVERIFY(!LocalApi::authorized(r, QStringLiteral("bad")));
    QVERIFY(!LocalApi::authorized(r, QString())); // empty token never authorises
  }

  void mapsSendBody() {
    Messaging::SendCommand cmd;
    QString err;
    QVERIFY(LocalApi::parseSendBody(
        "{\"to\":\"+34600\",\"message\":\"hi\",\"backend\":\"cloud\"}", &cmd,
        &err));
    QCOMPARE(cmd.to, QStringLiteral("+34600"));
    QCOMPARE(cmd.message, QStringLiteral("hi"));
    QCOMPARE(cmd.backend, Messaging::Backend::Cloud);

    // Defaults to the web backend when omitted.
    QVERIFY(LocalApi::parseSendBody("{\"to\":\"x\"}", &cmd, &err));
    QCOMPARE(cmd.backend, Messaging::Backend::Web);
  }

  void rejectsBadSendBody() {
    Messaging::SendCommand cmd;
    QString err;
    QVERIFY(!LocalApi::parseSendBody("not json", &cmd, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!LocalApi::parseSendBody("{\"message\":\"no recipient\"}", &cmd, &err));
    QVERIFY(!LocalApi::parseSendBody("{\"to\":\"x\",\"backend\":\"smoke\"}", &cmd, &err));
  }

  void buildsResponse() {
    QByteArray body = LocalApi::jsonField(QStringLiteral("status"),
                                          QStringLiteral("accepted"));
    QByteArray resp = LocalApi::buildResponse(202, body);
    QVERIFY(resp.startsWith("HTTP/1.1 202 Accepted\r\n"));
    QVERIFY(resp.contains("Content-Length: " + QByteArray::number(body.size())));
    QVERIFY(resp.contains("Connection: close"));
    QVERIFY(resp.endsWith(body));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The LocalApiServer end to end: a real loopback listener answering raw HTTP
// requests, covering onNewConnection/serviceRequest dispatch (auth, routing,
// the /send and /webhook endpoints) that the pure-parser tests above cannot.
class TstLocalApiServer : public QObject {
  Q_OBJECT

  // One request/response over loopback. Returns the full response bytes (the
  // server closes the connection after each reply, so we read to EOF).
  static QByteArray roundTrip(int port, const QByteArray &raw) {
    QTcpSocket sock;
    QByteArray resp;
    QObject::connect(&sock, &QIODevice::readyRead, &sock,
                     [&]() { resp += sock.readAll(); });
    // The server and this client share the test's event loop, so a blocking
    // waitForReadyRead would never let the server's newConnection slot run.
    // QSignalSpy::wait() spins a real loop instead, so both sides progress.
    QSignalSpy done(&sock, &QAbstractSocket::disconnected);
    sock.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(port));
    if (!sock.waitForConnected(3000))
      return QByteArray();
    sock.write(raw);
    sock.flush();
    done.wait(4000); // server replies then closes (Connection: close)
    resp += sock.readAll();
    return resp;
  }

  static QByteArray httpReq(const QByteArray &method, const QByteArray &path,
                            const QByteArray &auth = QByteArray(),
                            const QByteArray &body = QByteArray()) {
    QByteArray r = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!auth.isEmpty())
      r += "Authorization: Bearer " + auth + "\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) +
         "\r\nConnection: close\r\n\r\n" + body;
    return r;
  }

private slots:
  void init() {
    LocalApi::setEnabled(true);
    LocalApi::setToken(QStringLiteral("secret"));
    LocalApi::setPort(0); // ephemeral: let the OS pick a free loopback port
    CloudWebhook::setEnabled(false);
  }
  void cleanup() {
    LocalApi::setEnabled(false);
    LocalApi::setToken(QString());
    CloudWebhook::setEnabled(false);
  }

  void sendAcceptedEmitsCommand() {
    LocalApiServer server;
    QVERIFY(server.start());
    QVERIFY(server.isListening());
    const int port = server.listeningPort();
    QSignalSpy spy(&server, &LocalApiServer::sendRequested);
    const QByteArray resp = roundTrip(
        port, httpReq("POST", "/send", "secret", "{\"to\":\"x\"}"));
    QVERIFY(resp.startsWith("HTTP/1.1 202 Accepted\r\n"));
    QCOMPARE(spy.count(), 1);
  }

  void rejectsMissingOrWrongToken() {
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    const QByteArray noTok =
        roundTrip(port, httpReq("POST", "/send", QByteArray(), "{\"to\":\"x\"}"));
    QVERIFY(noTok.startsWith("HTTP/1.1 401 "));
    const QByteArray badTok =
        roundTrip(port, httpReq("POST", "/send", "nope", "{\"to\":\"x\"}"));
    QVERIFY(badTok.startsWith("HTTP/1.1 401 "));
  }

  void routingErrors() {
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    const QByteArray wrongMethod =
        roundTrip(port, httpReq("GET", "/send", "secret"));
    QVERIFY(wrongMethod.startsWith("HTTP/1.1 405 "));
    const QByteArray unknown =
        roundTrip(port, httpReq("POST", "/nope", "secret", "{}"));
    QVERIFY(unknown.startsWith("HTTP/1.1 404 "));
    const QByteArray badBody =
        roundTrip(port, httpReq("POST", "/send", "secret", "not json"));
    QVERIFY(badBody.startsWith("HTTP/1.1 400 "));
  }

  void webhookVerifyHandshake() {
    CloudWebhook::setEnabled(true);
    CloudWebhook::setVerifyToken(QStringLiteral("vtok"));
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    // Disabled path is checked first: without the enable it would 404. Here the
    // GET handshake echoes the challenge back on a matching verify token.
    const QByteArray ok = roundTrip(
        port, httpReq("GET", "/webhook?hub.mode=subscribe&hub.verify_token=vtok&"
                             "hub.challenge=98765"));
    QVERIFY(ok.startsWith("HTTP/1.1 200 "));
    QVERIFY(ok.endsWith("98765"));
    const QByteArray bad = roundTrip(
        port, httpReq("GET", "/webhook?hub.mode=subscribe&hub.verify_token=wrong&"
                             "hub.challenge=1"));
    QVERIFY(bad.startsWith("HTTP/1.1 403 "));
  }

  void webhookDisabledIs404() {
    // Webhook off, but the send API keeps the listener up.
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    const QByteArray resp = roundTrip(port, httpReq("GET", "/webhook"));
    QVERIFY(resp.startsWith("HTTP/1.1 404 "));
  }

  void webhookPostDeliversMessages() {
    CloudWebhook::setEnabled(true);
    CloudWebhook::setAppSecret(QString()); // no signature check configured
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    QSignalSpy got(&server, &LocalApiServer::webhookMessageReceived);
    const QByteArray payload =
        R"({"object":"whatsapp_business_account","entry":[{"changes":[{"value":{)"
        R"("messages":[{"from":"34600123456","id":"wamid.1","type":"text",)"
        R"("text":{"body":"hola"}},{"from":"34600999999","id":"wamid.2",)"
        R"("type":"image"}]}}]}]})";
    const QByteArray resp =
        roundTrip(port, httpReq("POST", "/webhook", QByteArray(), payload));
    QVERIFY(resp.startsWith("HTTP/1.1 200 "));
    QCOMPARE(got.count(), 1); // only the text message carries a body
    CloudWebhook::setAppSecret(QString());
  }

  void webhookPostRejectsBadSignature() {
    CloudWebhook::setEnabled(true);
    CloudWebhook::setAppSecret(QStringLiteral("secret")); // now checked
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    QByteArray req = httpReq("POST", "/webhook", QByteArray(), "{}");
    req.replace("Connection: close",
                "x-hub-signature-256: sha256=deadbeef\r\nConnection: close");
    const QByteArray resp = roundTrip(port, req);
    QVERIFY(resp.startsWith("HTTP/1.1 401 "));
    CloudWebhook::setAppSecret(QString());
  }

  void startFailsWhenUnconfigured() {
    LocalApi::setEnabled(false);
    LocalApi::setToken(QString());
    CloudWebhook::setEnabled(false);
    LocalApiServer server;
    QString error;
    QVERIFY(!server.start(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!server.isListening());
  }

  void startFailsWhenPortInUse() {
    LocalApi::setPort(0); // A takes an ephemeral port
    LocalApiServer a;
    QVERIFY(a.start());
    const int port = a.listeningPort();
    LocalApi::setPort(port); // force B onto the same, already-bound port
    LocalApiServer b;
    QString error;
    QVERIFY(!b.start(&error)); // listen() fails
    QVERIFY(!error.isEmpty());
    LocalApi::setPort(0);
  }

  void stopClosesTheListener() {
    LocalApiServer server;
    QVERIFY(server.start());
    QVERIFY(server.isListening());
    server.stop();
    QVERIFY(!server.isListening());
  }

  void webhookRejectsOtherMethods() {
    CloudWebhook::setEnabled(true);
    LocalApiServer server;
    QVERIFY(server.start());
    const int port = server.listeningPort();
    const QByteArray resp =
        roundTrip(port, httpReq("PUT", "/webhook", QByteArray(), "{}"));
    QVERIFY(resp.startsWith("HTTP/1.1 405 "));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cloud API webhook: verification, signature and payload parsing — pure.
class TstCloudWebhook : public QObject {
  Q_OBJECT
private slots:
  void parsesQuery() {
    auto q = CloudWebhook::parseQuery(
        QStringLiteral("/webhook?hub.mode=subscribe&hub.verify_token=tok&"
                       "hub.challenge=42"));
    QCOMPARE(q.value("hub.mode"), QStringLiteral("subscribe"));
    QCOMPARE(q.value("hub.verify_token"), QStringLiteral("tok"));
    QCOMPARE(q.value("hub.challenge"), QStringLiteral("42"));
    QVERIFY(CloudWebhook::parseQuery(QStringLiteral("/webhook")).isEmpty());
  }

  void verifyChallenge() {
    auto q = CloudWebhook::parseQuery(
        QStringLiteral("/webhook?hub.mode=subscribe&hub.verify_token=tok&"
                       "hub.challenge=42"));
    QCOMPARE(CloudWebhook::verifyChallenge(q, QStringLiteral("tok")),
             QStringLiteral("42"));
    // Wrong token, wrong mode and empty expected token all reject.
    QVERIFY(CloudWebhook::verifyChallenge(q, QStringLiteral("nope")).isEmpty());
    QVERIFY(CloudWebhook::verifyChallenge(q, QString()).isEmpty());
    auto bad = CloudWebhook::parseQuery(
        QStringLiteral("/webhook?hub.mode=unsubscribe&hub.verify_token=tok"));
    QVERIFY(CloudWebhook::verifyChallenge(bad, QStringLiteral("tok")).isEmpty());
  }

  void verifiesSignature() {
    const QByteArray body = "{\"hello\":\"world\"}";
    const QByteArray secret = "s3cr3t";
    const QByteArray hex =
        QMessageAuthenticationCode::hash(body, secret,
                                         QCryptographicHash::Sha256)
            .toHex();
    QVERIFY(CloudWebhook::verifySignature(body, "sha256=" + QString::fromLatin1(hex),
                                          QString::fromUtf8(secret)));
    QVERIFY(!CloudWebhook::verifySignature(body, "sha256=deadbeef",
                                           QString::fromUtf8(secret)));
    QVERIFY(!CloudWebhook::verifySignature(body, "nosha", QString::fromUtf8(secret)));
    // No secret configured -> the check is skipped (returns true).
    QVERIFY(CloudWebhook::verifySignature(body, QString(), QString()));
  }

  void parsesIncoming() {
    const QByteArray payload = R"({
      "object":"whatsapp_business_account",
      "entry":[{"changes":[{"value":{
        "messages":[
          {"from":"34600123456","id":"wamid.1","type":"text",
           "text":{"body":"hola"}},
          {"from":"34600999999","id":"wamid.2","type":"image"}
        ]}}]}]
    })";
    auto msgs = CloudWebhook::parseIncoming(payload);
    QCOMPARE(msgs.size(), 2);
    QCOMPARE(msgs.at(0).from, QStringLiteral("34600123456"));
    QCOMPARE(msgs.at(0).text, QStringLiteral("hola"));
    QCOMPARE(msgs.at(0).type, QStringLiteral("text"));
    QCOMPARE(msgs.at(1).type, QStringLiteral("image"));
    QVERIFY(msgs.at(1).text.isEmpty()); // non-text carries no body
    QVERIFY(CloudWebhook::parseIncoming("not json").isEmpty());
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
// ─────────────────────────────────────────────────────────────────────────────
// Page-zoom clamping — pure. Guards the bounds used by Ctrl +/- and the injected
// zoom buttons so a runaway value can't make the UI unusable.
class TstZoom : public QObject {
  Q_OBJECT
private slots:
  void clampsWithinBounds() {
    QCOMPARE(clampZoom(1.0), 1.0);
    QCOMPARE(clampZoom(kMinZoomFactor), kMinZoomFactor);
    QCOMPARE(clampZoom(kMaxZoomFactor), kMaxZoomFactor);
  }
  void clampsOutOfRange() {
    QCOMPARE(clampZoom(0.05), kMinZoomFactor);   // far too small
    QCOMPARE(clampZoom(-1.0), kMinZoomFactor);   // negative
    QCOMPARE(clampZoom(9.0), kMaxZoomFactor);    // far too large
    // A normal Ctrl+- step from the floor stays at the floor.
    QCOMPARE(clampZoom(kMinZoomFactor - 0.1), kMinZoomFactor);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
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
    Performance::setWebrtcPipeWire(false);
    Performance::setJsMemoryLimitMb(0);
    Performance::setOptimizeForSize(false); // default is on; isolate other cases
    Performance::setCacheType(QStringLiteral("disk"));
    Performance::setCacheMaxMb(0);
    Performance::setFontHinting(QString()); // default: follow the system
    Performance::setSuspendInactiveAccounts(false);
    Performance::setUnloadOffscreenWindows(false); // else a prior run leaks in

    // No start-up crash recovery pending: level 0, watch disarmed.
    Performance::markStartupSucceeded();
  }

  void emptyWhenAllOff() {
    QCOMPARE(Performance::chromiumFlagFragment(), QString());
  }

  // Start-up crash recovery (issue #3): a launch that arms the watch but never
  // reports success is treated as a crash on the next evaluateStartup(), which
  // escalates the recovery level (capped) and adds safe-rendering flags. A
  // successful load resets it.
  void recoveryEscalatesThenResets() {
    QCOMPARE(Performance::recoveryLevel(), 0);
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--disable-software-rasterizer")));

    // No crash pending → evaluateStartup() is a no-op.
    Performance::evaluateStartup();
    QCOMPARE(Performance::recoveryLevel(), 0);

    // Arm, then a crash: evaluateStartup() sees the pending flag → level 1.
    Performance::armStartupWatch();
    Performance::evaluateStartup();
    QCOMPARE(Performance::recoveryLevel(), 1);
    const QString l1 = Performance::chromiumFlagFragment();
    QVERIFY(l1.contains(QLatin1String("--disable-gpu")));
    QVERIFY(l1.contains(QLatin1String("--disable-software-rasterizer")));
    QVERIFY(!l1.contains(QLatin1String("--in-process-gpu")));

    // A single crash is counted once: evaluating again without re-arming does
    // not bump the level.
    Performance::evaluateStartup();
    QCOMPARE(Performance::recoveryLevel(), 1);

    // A second crash → level 2 adds the stronger flags.
    Performance::armStartupWatch();
    Performance::evaluateStartup();
    QCOMPARE(Performance::recoveryLevel(), 2);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--in-process-gpu")));

    // Capped at 2.
    Performance::armStartupWatch();
    Performance::evaluateStartup();
    QCOMPARE(Performance::recoveryLevel(), 2);

    // A clean load resets everything.
    Performance::markStartupSucceeded();
    QCOMPARE(Performance::recoveryLevel(), 0);
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--disable-software-rasterizer")));
  }

  // The recovery flags must not duplicate a switch the user's own GPU settings
  // already added.
  void recoveryDoesNotDuplicateFlags() {
    Performance::setDisableGpu(true); // user already forced --disable-gpu
    Performance::armStartupWatch();
    Performance::evaluateStartup();   // → level 1, also wants --disable-gpu
    const QStringList tokens =
        Performance::chromiumFlagFragment().split(QLatin1Char(' '),
                                                  Qt::SkipEmptyParts);
    QCOMPARE(tokens.count(QStringLiteral("--disable-gpu")), 1);
    Performance::markStartupSucceeded();
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

  // Screen sharing on Wayland needs the PipeWire capturer; it is on by default
  // on Linux (the init() above turns it off so the other cases stay isolated).
  void webrtcPipeWireFlag() {
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("WebRTCPipeWireCapturer")));
    Performance::setWebrtcPipeWire(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--enable-features=WebRTCPipeWireCapturer")));
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

  // optimize-for-size (issue #15) trims the baseline heap and is on by default.
  // It shares the single --js-flags token with the heap cap: Qt splits the env
  // var on whitespace, so only one sub-flag can travel, and an explicit cap wins.
  void optimizeForSizeFlag() {
    // Not present once init() has turned it off.
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags")));

    Performance::setOptimizeForSize(true);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags=--optimize-for-size")));

    // An explicit heap cap takes the slot (single space-free token, cap wins).
    Performance::setJsMemoryLimitMb(512);
    const QString withCap = Performance::chromiumFlagFragment();
    QVERIFY(withCap.contains(QLatin1String("--js-flags=--max-old-space-size=512")));
    QVERIFY(!withCap.contains(QLatin1String("--optimize-for-size")));

    // Removing the cap falls back to optimize-for-size again.
    Performance::setJsMemoryLimitMb(0);
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--js-flags=--optimize-for-size")));
    Performance::setOptimizeForSize(false);
  }

  // The default (a fresh install, no keys written) has optimize-for-size on.
  void optimizeForSizeDefaultsOn() {
    Performance::settings().remove(QStringLiteral("perf/optimizeForSize"));
    QVERIFY(Performance::optimizeForSize());
    Performance::setOptimizeForSize(false); // restore the isolated baseline
  }

  // Font hinting (issue #37): default follows the system (no flag); a set level
  // maps to --font-render-hinting; an unknown value is ignored.
  void fontHintingFlag() {
    QCOMPARE(Performance::fontHinting(), QString());       // default
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--font-render-hinting")));

    Performance::setFontHinting(QStringLiteral("slight"));
    QCOMPARE(Performance::fontHinting(), QStringLiteral("slight"));
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--font-render-hinting=slight")));

    Performance::setFontHinting(QStringLiteral("none"));
    QVERIFY(Performance::chromiumFlagFragment().contains(
        QLatin1String("--font-render-hinting=none")));

    // Anything not in {none,slight,medium,full} adds no flag.
    Performance::setFontHinting(QStringLiteral("bogus"));
    QVERIFY(!Performance::chromiumFlagFragment().contains(
        QLatin1String("--font-render-hinting")));

    Performance::setFontHinting(QString()); // restore the isolated baseline
  }

  // The selected-tab tint follows the palette rather than being a fixed colour,
  // so a light theme and a dark one get different tints and a theme switch is
  // picked up.
  //
  // NOTE what this does NOT cover: the re-entrancy guard in refreshSelectionTint().
  // Setting a stylesheet on a widget that has never been shown does not make Qt
  // re-resolve its style under the offscreen platform, so the event that would
  // re-enter the handler is never sent and this test passes either way — verified
  // by removing the guard and watching it still pass. The guard is there because
  // the recursion is evident from the code, not because this test proves it.
  void tabTintFollowsPalette() {
    AccountTabBar bar;
    bar.addTab(QStringLiteral("one"));
    bar.addTab(QStringLiteral("two"));

    QPalette dark = bar.palette();
    dark.setColor(QPalette::Window, QColor(30, 30, 30));
    bar.setPalette(dark);
    const QString darkSheet = bar.styleSheet();
    // The tabs that are NOT on screen carry the tint; the one that is stays as
    // the platform draws it, and is told apart by being the only untinted one.
    QVERIFY(darkSheet.contains(QLatin1String("QTabBar::tab:!selected")));

    QPalette light = bar.palette();
    light.setColor(QPalette::Window, QColor(240, 240, 240));
    bar.setPalette(light);
    const QString lightSheet = bar.styleSheet();
    QVERIFY(lightSheet.contains(QLatin1String("QTabBar::tab:!selected")));

    // The tint follows the palette rather than being fixed, so a light theme and
    // a dark one must not end up with the same colour.
    QVERIFY(darkSheet != lightSheet);
  }

  // Idle account suspension (#1): only a background, off-screen, idle account is
  // frozen; the active or any visible view never is.
  void suspendDecision() {
    QVERIFY(!Performance::shouldSuspendAccount(false, false, false, 9999, 60)); // off
    QVERIFY(!Performance::shouldSuspendAccount(true, true, false, 9999, 60));   // active
    QVERIFY(!Performance::shouldSuspendAccount(true, false, true, 9999, 60));   // visible
    QVERIFY(!Performance::shouldSuspendAccount(true, false, false, 30, 60));    // not idle yet
    QVERIFY(Performance::shouldSuspendAccount(true, false, false, 120, 60));    // suspend

    Performance::setSuspendInactiveAccounts(true);
    QVERIFY(Performance::suspendInactiveAccounts());
    Performance::setSuspendAfterMinutes(0);
    QCOMPARE(Performance::suspendAfterMinutes(), 1); // clamped to >= 1
    Performance::setSuspendInactiveAccounts(false);
    Performance::setSuspendAfterMinutes(15);
  }

  // The window half of the same setting (#25): the account a window was showing
  // goes with the window, minimised or put away — but only with both switches on,
  // and only after the same wait as the rule above. Dogfood round 27: it used to
  // unload ten seconds after the window went, whatever the delay was set to, and
  // "it should happen in conjunction with the 'Unload inactive accounts' setting".
  void offscreenWindowUnloadDecision() {
    QVERIFY(!Performance::shouldUnloadWithWindow(false, true, true, 9999, 60)); // unloading off
    QVERIFY(!Performance::shouldUnloadWithWindow(true, false, true, 9999, 60)); // option off
    QVERIFY(!Performance::shouldUnloadWithWindow(true, true, false, 9999, 60)); // on screen
    QVERIFY(!Performance::shouldUnloadWithWindow(true, true, true, 30, 60));    // away, not long enough
    QVERIFY(Performance::shouldUnloadWithWindow(true, true, true, 60, 60));     // the wait is up
    QVERIFY(Performance::shouldUnloadWithWindow(true, true, true, 900, 60));

    // It is off until asked for: someone who turns on unloading gets the idle rule
    // and nothing more, so notifications keep arriving while the window is away.
    QVERIFY(!Performance::unloadOffscreenWindows());
    Performance::setUnloadOffscreenWindows(true);
    QVERIFY(Performance::unloadOffscreenWindows());
    Performance::setUnloadOffscreenWindows(false);
    QVERIFY(!Performance::unloadOffscreenWindows());
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

// Monochrome tray glyph mask (issue #14): the SVG is preferred, but the helper
// must fall back to the colour icon's shape when the SVG can't be rendered, so
// the tray is never left blank.
class TstTrayIcon : public QObject {
  Q_OBJECT
private slots:
  void fullyTransparentDetectsEmpty() {
    QImage empty(8, 8, QImage::Format_ARGB32_Premultiplied);
    empty.fill(Qt::transparent);
    QVERIFY(TrayIcon::isFullyTransparent(empty));
    QVERIFY(TrayIcon::isFullyTransparent(QImage())); // null counts as empty

    QImage opaque(8, 8, QImage::Format_ARGB32_Premultiplied);
    opaque.fill(Qt::white);
    QVERIFY(!TrayIcon::isFullyTransparent(opaque));

    QImage oneDot(8, 8, QImage::Format_ARGB32_Premultiplied);
    oneDot.fill(Qt::transparent);
    oneDot.setPixelColor(3, 3, QColor(0, 0, 0, 1)); // a single non-zero alpha
    QVERIFY(!TrayIcon::isFullyTransparent(oneDot));
  }

  void svgRendersGlyph() {
    // The real bundled symbolic icon renders to a non-empty mask of the size.
    const QImage m = TrayIcon::monochromeGlyphMask(
        QStringLiteral(":/icons/app/whatly-symbolic.svg"),
        QStringLiteral(":/icons/app/notification/whatly-notify.png"), 64);
    QCOMPARE(m.size(), QSize(64, 64));
    QVERIFY(!TrayIcon::isFullyTransparent(m));
  }

  void fallsBackToPngWhenSvgFails() {
    // A bogus SVG path forces the fallback: the colour PNG's shape must still
    // give a visible mask.
    const QImage m = TrayIcon::monochromeGlyphMask(
        QStringLiteral(":/does/not/exist.svg"),
        QStringLiteral(":/icons/app/notification/whatly-notify.png"), 64);
    QVERIFY(!TrayIcon::isFullyTransparent(m));
  }

  void emptyWhenBothSourcesFail() {
    // Both sources missing → transparent, so the caller degrades to colour.
    const QImage m = TrayIcon::monochromeGlyphMask(
        QStringLiteral(":/does/not/exist.svg"),
        QStringLiteral(":/also/missing.png"), 32);
    QCOMPARE(m.size(), QSize(32, 32));
    QVERIFY(TrayIcon::isFullyTransparent(m));
  }

  // The #14 regression: with zero unread messages the tray must still honour the
  // monochrome choice. The idle icon used to bypass composition and always show
  // the colour icon, so the toggle "did nothing" whenever the inbox was clear.
  void idleIconHonoursMonochrome() {
    const QImage colour = TrayIcon::composeTrayImage(0, false, true, 64);
    const QImage mono = TrayIcon::composeTrayImage(0, true, true, 64);
    QCOMPARE(colour.size(), QSize(64, 64));
    QCOMPARE(mono.size(), QSize(64, 64));
    QVERIFY(!TrayIcon::isFullyTransparent(colour));
    QVERIFY(!TrayIcon::isFullyTransparent(mono));
    // The two must actually differ — that is the whole point of the setting.
    QVERIFY(colour != mono);
  }

  // A count badge changes the monochrome icon (the number is drawn on top).
  void monochromeCountDiffersFromIdle() {
    const QImage idle = TrayIcon::composeTrayImage(0, true, true, 64);
    const QImage three = TrayIcon::composeTrayImage(3, true, true, 64);
    QVERIFY(idle != three);
  }

  // What the badge says, which is the whole of the rule: the number up to
  // ninety-nine, "99+" past it, nothing at zero. The count itself is no longer
  // clamped — it used to stop at ten, which was invisible while the count came
  // from the window title and under-reported, and became permanent once the count
  // was real.
  void badgeSaysTheNumberUpToNinetyNine() {
    QVERIFY(TrayIcon::badgeText(0).isEmpty());
    QVERIFY(TrayIcon::badgeText(-3).isEmpty());
    QCOMPARE(TrayIcon::badgeText(1), QStringLiteral("1"));
    QCOMPARE(TrayIcon::badgeText(9), QStringLiteral("9"));
    QCOMPARE(TrayIcon::badgeText(10), QStringLiteral("10"));
    QCOMPARE(TrayIcon::badgeText(42), QStringLiteral("42"));
    QCOMPARE(TrayIcon::badgeText(99), QStringLiteral("99"));
    QCOMPARE(TrayIcon::badgeText(100), QStringLiteral("99+"));
    QCOMPARE(TrayIcon::badgeText(5000), QStringLiteral("99+"));
  }

  // Past nine the colour icon draws the badge itself, so counts that used to be
  // one and the same picture — everything from ten up wore whatly-notify-10.png,
  // a bare "+" with no digit — now differ from each other.
  void colourCountsPastNineAreDrawnAndDiffer() {
    const QImage nine = TrayIcon::composeTrayImage(9, false, true, 64);
    const QImage ten = TrayIcon::composeTrayImage(10, false, true, 64);
    const QImage forty = TrayIcon::composeTrayImage(42, false, true, 64);
    QVERIFY(nine != ten);
    QVERIFY(ten != forty);
    QVERIFY(!TrayIcon::isFullyTransparent(ten));
    // Only the digits stop at ninety-nine, so those two are the same picture.
    QCOMPARE(TrayIcon::composeTrayImage(100, false, true, 64),
             TrayIcon::composeTrayImage(5000, false, true, 64));
    QVERIFY(TrayIcon::composeTrayImage(99, false, true, 64) !=
            TrayIcon::composeTrayImage(100, false, true, 64));
  }

  // The monochrome mode said "9+" past nine while the colour one said "+", so the
  // two disagreed about the same inbox. Both now draw the same text.
  void monochromeCountsAgreeWithColour() {
    QVERIFY(TrayIcon::composeTrayImage(9, true, true, 64) !=
            TrayIcon::composeTrayImage(10, true, true, 64));
    QVERIFY(TrayIcon::composeTrayImage(10, true, true, 64) !=
            TrayIcon::composeTrayImage(42, true, true, 64));
    QCOMPARE(TrayIcon::composeTrayImage(100, true, true, 64),
             TrayIcon::composeTrayImage(5000, true, true, 64));
  }

  // A disconnected state dims the icon, so it differs from the connected one.
  void disconnectedIsDimmed() {
    const QImage up = TrayIcon::composeTrayImage(0, false, true, 64);
    const QImage down = TrayIcon::composeTrayImage(0, false, false, 64);
    QVERIFY(up != down);
  }
};

// Drag-and-drop attachments (issue #285): the script generator rebuilds dropped
// files as a DataTransfer and dispatches a synthetic drop on the open chat.
class TstDropAttach : public QObject {
  Q_OBJECT
private slots:
  void emptyGivesNoScript() {
    QVERIFY(DropAttach::scriptSource({}).isEmpty());
  }

  void buildsDropScript() {
    QList<DropAttach::File> files;
    files.append({QStringLiteral("photo.png"), QStringLiteral("image/png"),
                  QStringLiteral("QUJD")}); // "ABC"
    files.append({QStringLiteral("doc.pdf"),
                  QStringLiteral("application/pdf"), QStringLiteral("REVG")});
    const QString js = DropAttach::scriptSource(files);
    QVERIFY(!js.isEmpty());
    // Rebuilds a DataTransfer and hands it to the composer via a paste event.
    QVERIFY(js.contains(QLatin1String("new DataTransfer()")));
    QVERIFY(js.contains(QLatin1String("ClipboardEvent")));
    QVERIFY(js.contains(QLatin1String("\"paste\"")));
    QVERIFY(js.contains(QLatin1String("#main")));
    // Both files (name, type and payload) are embedded as JSON.
    QVERIFY(js.contains(QLatin1String("photo.png")));
    QVERIFY(js.contains(QLatin1String("application/pdf")));
    QVERIFY(js.contains(QLatin1String("QUJD")));
    QVERIFY(js.contains(QLatin1String("REVG")));
  }

  // A filename with characters special to JSON/JS must stay safely quoted.
  void escapesAwkwardNames() {
    QList<DropAttach::File> files;
    files.append({QStringLiteral("a\"b'c.png"), QStringLiteral("image/png"),
                  QStringLiteral("QQ==")});
    const QString js = DropAttach::scriptSource(files);
    QVERIFY(!js.isEmpty());
    // The double quote is JSON-escaped, so the script stays well-formed.
    QVERIFY(js.contains(QLatin1String("a\\\"b'c.png")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// DropResolve: turning a drop into readable paths. A normal install reads the
// real files directly (no portal), which is what #34 fixed. The portal fallback
// itself needs D-Bus and is not exercised here.
class TstDropResolve : public QObject {
  Q_OBJECT
private slots:
  void nullMimeIsEmpty() {
    QCOMPARE(DropResolve::droppedFilePaths(nullptr), QStringList());
  }

  // A readable local file is returned directly.
  void readableLocalFileUsed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QMimeData m;
    m.setUrls({QUrl::fromLocalFile(path)});
    const QStringList got = DropResolve::droppedFilePaths(&m);
    QCOMPARE(got.size(), 1);
    QVERIFY(got.first().endsWith(QStringLiteral("a.jpg")));
  }

  // The #34 fix: when a readable local path is present, it is used directly and
  // the FileTransfer portal is never consulted, even if the portal mime is set.
  void readableLocalWinsOverPortalMime() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("b.png"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("y");
    f.close();

    QMimeData m;
    m.setUrls({QUrl::fromLocalFile(path)});
    // A portal handle that, if consulted, would resolve to something else (or
    // fail). It must be ignored because the local file is readable.
    m.setData(QStringLiteral("application/vnd.portal.filetransfer"),
              QByteArrayLiteral("bogus-key"));
    const QStringList got = DropResolve::droppedFilePaths(&m);
    QCOMPARE(got.size(), 1);
    QVERIFY(got.first().endsWith(QStringLiteral("b.png")));
  }

  // A non-local URL with no portal handle yields nothing (no crash, no bogus
  // path).
  void nonLocalYieldsEmpty() {
    QMimeData m;
    m.setUrls({QUrl(QStringLiteral("https://example.com/x"))});
    QVERIFY(DropResolve::droppedFilePaths(&m).isEmpty());
  }
};

// DropReader::plan: what a drop will read, worked out from the sizes before any
// reading starts. It sizes the progress bar and names the files left out, which
// used to be a terminal-only warning.
class TstDropReader : public QObject {
  Q_OBJECT

  // A file of exactly the requested size.
  static QString makeFile(const QTemporaryDir &dir, const QString &name,
                          qint64 bytes) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
      return QString();
    if (bytes > 0)
      f.write(QByteArray(static_cast<int>(bytes), 'x'));
    f.close();
    return path;
  }

private slots:
  void emptyPathsPlanNothing() {
    const DropReader::Plan p = DropReader::plan({});
    QVERIFY(p.accepted.isEmpty());
    QVERIFY(p.tooLarge.isEmpty());
    QCOMPARE(p.totalBytes, 0);
  }

  // Normal files are accepted and their bytes summed, so the bar has a total.
  void acceptsAndSumsSizes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = makeFile(dir, QStringLiteral("a.bin"), 100);
    const QString b = makeFile(dir, QStringLiteral("b.bin"), 250);

    const DropReader::Plan p = DropReader::plan({a, b});
    QCOMPARE(p.accepted.size(), 2);
    QCOMPARE(p.totalBytes, 350);
    QVERIFY(p.tooLarge.isEmpty());
  }

  // Empty files and directories have nothing to attach, and are not reported as
  // too large either.
  void skipsEmptyAndDirectories() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString empty = makeFile(dir, QStringLiteral("empty.bin"), 0);

    const DropReader::Plan p = DropReader::plan({empty, dir.path()});
    QVERIFY(p.accepted.isEmpty());
    QVERIFY(p.tooLarge.isEmpty());
    QCOMPARE(p.totalBytes, 0);
  }

  // Over the cap the file is named rather than silently dropped, and a file that
  // still fits is kept.
  void oversizeIsReportedNotSilent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString small = makeFile(dir, QStringLiteral("small.bin"), 10);
    // Sparse, so the test does not write 64 MB to disk.
    const QString huge = dir.filePath(QStringLiteral("huge.bin"));
    QFile f(huge);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.resize(DropReader::kMaxTotalBytes + 1));
    f.close();

    const DropReader::Plan p = DropReader::plan({small, huge});
    QCOMPARE(p.accepted.size(), 1);
    QVERIFY(p.accepted.first().endsWith(QStringLiteral("small.bin")));
    QCOMPARE(p.tooLarge, QStringList{QStringLiteral("huge.bin")});
    QCOMPARE(p.totalBytes, 10);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The Flatpak manifests must grant read access to the standard media folders so
// files dragged from a host file manager can be attached (#32), while staying
// scoped: no whole-home or host access, which the Flathub sandbox forbids.
class TstFlatpakManifest : public QObject {
  Q_OBJECT
  static QString read(const QString &rel) {
    QFile f(QStringLiteral(WHATLY_SOURCE_DIR) + QLatin1Char('/') + rel);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return QString();
    return QString::fromUtf8(f.readAll());
  }
private slots:
  void mediaDirsGrantedAndScoped() {
    const QStringList manifests = {
        QStringLiteral("packaging/flatpak/net.shakaran.whatly.yml"),
        QStringLiteral("packaging/flathub/net.shakaran.whatly.yml")};
    for (const QString &rel : manifests) {
      const QString m = read(rel);
      QVERIFY2(!m.isEmpty(), qPrintable(rel));
      // The read-only media/document grants that make drag-drop work (#32).
      for (const char *grant :
           {"--filesystem=xdg-download", "--filesystem=xdg-pictures:ro",
            "--filesystem=xdg-videos:ro", "--filesystem=xdg-documents:ro",
            "--filesystem=xdg-music:ro"})
        QVERIFY2(m.contains(QLatin1String(grant)),
                 qPrintable(rel + ": missing " + grant));
      // Stay scoped: never widen to the whole home or the host filesystem.
      QVERIFY2(!m.contains(QLatin1String("--filesystem=home")),
               qPrintable(rel + ": must not grant home"));
      QVERIFY2(!m.contains(QLatin1String("--filesystem=host")),
               qPrintable(rel + ": must not grant host"));
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ChatNav: the JS builders for the tray "recent unread" feature (#3). The
// runtime behaviour is verified live; here we check structure and escaping.
class TstChatNav : public QObject {
  Q_OBJECT
private slots:
  void unreadScript() {
    const QString js = ChatNav::unreadChatsScript(6);
    QVERIFY(js.contains(QLatin1String("#pane-side")));
    QVERIFY(js.contains(QLatin1String("JSON.stringify")));
    QVERIFY(js.contains(QLatin1String(">= 6"))); // the limit was substituted
    QVERIFY(!ChatNav::unreadChatsScript(0).isEmpty()); // clamped, still valid
  }
  void currentChatNameScript() {
    const QString js = ChatNav::currentChatNameScript();
    QVERIFY(js.contains(QLatin1String("#main")));   // reads the open conversation
    QVERIFY(js.contains(QLatin1String("header")));
    QVERIFY(js.contains(QLatin1String("span[title]")));
  }
  void unreadDigestScript() {
    const QString js = ChatNav::unreadDigestScript(20);
    QVERIFY(js.contains(QLatin1String("#pane-side")));
    QVERIFY(js.contains(QLatin1String("JSON.stringify")));
    QVERIFY(js.contains(QLatin1String("preview")));   // carries a preview
    QVERIFY(js.contains(QLatin1String(">= 20")));      // the limit substituted
    QVERIFY(!ChatNav::unreadDigestScript(0).isEmpty()); // clamped, still valid
  }
  void unreadSummaryReadsTheDatabaseAndFallsBackToTheList() {
    const QString js = ChatNav::unreadSummaryScript(true, false);
    // WhatsApp's own store, which is what makes the count independent of how
    // much of the virtualised list happens to be drawn.
    QVERIFY(js.contains(QLatin1String("model-storage")));
    QVERIFY(js.contains(QLatin1String("unreadCount")));
    // Both archived and muted are the user's call, so both are read from the
    // record and both are gated on a flag rather than hard-coded.
    QVERIFY(js.contains(QLatin1String("c.archive")));
    QVERIFY(js.contains(QLatin1String("muteExpiration")));
    // A chat marked unread by hand carries no message count and still counts.
    QVERIFY(js.contains(QLatin1String("markedUnread")));
    // And the list is still there to answer while the first read is in flight,
    // since runJavaScript cannot await a promise.
    QVERIFY(js.contains(QLatin1String("#pane-side")));
    QVERIFY(js.contains(QLatin1String("__whatlyUnread")));
    // A read that did not finish must not be reported as a confident zero: the
    // cursor sets `walked` only when it reaches the end of the store.
    QVERIFY(js.contains(QLatin1String("walked = true")));
    QVERIFY(js.contains(QLatin1String("return walked ?")));
    QVERIFY(js.contains(QLatin1String("JSON.stringify")));
  }
  void unreadSummaryCarriesTheUsersFilters() {
    const QString both = ChatNav::unreadSummaryScript(true, true);
    QVERIFY(both.contains(QLatin1String("INCLUDE_MUTED = true")));
    QVERIFY(both.contains(QLatin1String("INCLUDE_ARCHIVED = true")));
    const QString neither = ChatNav::unreadSummaryScript(false, false);
    QVERIFY(neither.contains(QLatin1String("INCLUDE_MUTED = false")));
    QVERIFY(neither.contains(QLatin1String("INCLUDE_ARCHIVED = false")));
    // The page keeps the last answer, so it has to know which question it was
    // an answer to — otherwise a changed setting shows the old number.
    QVERIFY(both.contains(QLatin1String("W.key !== key")));
  }
  void openScriptEscapesName() {
    const QString js =
        ChatNav::openChatByNameScript(QStringLiteral("a\"b'c\n<x>"));
    QVERIFY(js.contains(QLatin1String("pointerdown"))); // the click sequence
    QVERIFY(js.contains(QLatin1String("getBoundingClientRect")));
    // The name is JSON-escaped: the quote and newline are backslash-escaped,
    // so no raw quote/newline can break out of the string literal.
    QVERIFY(js.contains(QLatin1String("a\\\"b")));
    QVERIFY(js.contains(QLatin1String("\\n")));
    // It waits for the chat list instead of deciding on the first look: this also
    // runs against a page built moments earlier, for an account that had none
    // until its chat was picked out of the tray.
    QVERIFY(js.contains(QLatin1String("setTimeout")));
  }
  void focusSearchScriptShape() {
    const QString js = ChatNav::focusSearchScript();
    QVERIFY(js.contains(QLatin1String(".focus()")));
    // With a conversation open, Ctrl+F must mean the search WITHIN it — the
    // magnifier in the chat header — not the chat list's box. data-icon is
    // WhatsApp's own attribute; the classes around it are obfuscated.
    QVERIFY(js.contains(QLatin1String("[data-icon^=\"search\"]")));
    QVERIFY(js.contains(QLatin1String("#main")));
    // The panel's field is identified by being in neither pane, which is also
    // what keeps this off the message composer — that lives inside #main.
    QVERIFY(js.contains(QLatin1String("e.closest('#main')")));
    // A locale-independent fallback is required for the list search: naming the
    // English (or Spanish) aria-label alone would leave the key dead elsewhere.
    QVERIFY(js.contains(QLatin1String("#side input")));
    // Retries, because expanding a collapsed chat list is a round trip through
    // the app and the box does not exist yet when the script first runs.
    QVERIFY(js.contains(QLatin1String("setTimeout")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// AccountTabBar: a tab being dragged has to be identified by its account, never
// by the slot it was pressed in. QTabBar reorders tabs live under the cursor, so
// a slot number captured on press stops meaning that account as soon as the drag
// moves sideways — and tearing off read that slot, so the neighbour was torn out
// instead. The mouse path itself ends in QDrag::exec(), which blocks and cannot
// be driven from a test; what is checked here is the identity it now relies on.
class TstAccountTabBar : public QObject {
  Q_OBJECT
private slots:
  void accountFollowsItsTabThroughAReorder() {
    AccountTabBar bar;
    for (const QString &id : {QStringLiteral("a1"), QStringLiteral("a2"),
                              QStringLiteral("a3"), QStringLiteral("a4")})
      bar.setTabData(bar.addTab(id), id);
    bar.addTab(QStringLiteral("+")); // the affordance, deliberately without data

    QCOMPARE(bar.indexOfAccount(QStringLiteral("a3")), 2);
    // moveTab is exactly what QTabBar's live reorder does while a tab is dragged
    // past its neighbour, which is the trajectory that produced the wrong window.
    bar.moveTab(2, 1);
    // The slot that was pressed now holds a different account …
    QCOMPARE(bar.tabData(2).toString(), QStringLiteral("a2"));
    // … while the account pressed is still found, one slot to the left.
    QCOMPARE(bar.indexOfAccount(QStringLiteral("a3")), 1);
    // The "+" tab is not an account, and neither is anything unknown.
    QCOMPARE(bar.indexOfAccount(QString()), -1);
    QCOMPARE(bar.indexOfAccount(QStringLiteral("nope")), -1);
  }

  // The DEFAULT account's id is the empty string (MainWindow::Account::id), and
  // the "+" affordance is the tab with no data at all. Telling those two apart by
  // asking whether the id is empty makes the default account — the only account
  // most people have — the one account that cannot be dragged out of the strip.
  // It is the validity of the tab data that separates them, not the id.
  void defaultAccountIsATabLikeAnyOther() {
    AccountTabBar bar;
    bar.setTabData(bar.addTab(QStringLiteral("Default")), QString()); // id ""
    bar.setTabData(bar.addTab(QStringLiteral("Work")), QStringLiteral("w1"));
    bar.addTab(QStringLiteral("+"));  // no data: not an account

    QCOMPARE(bar.indexOfAccount(QString()), 0);
    QCOMPARE(bar.indexOfAccount(QStringLiteral("w1")), 1);
    // The affordance is never an account, and its slot must not be reachable by
    // asking for an id that no tab holds.
    QVERIFY(!bar.tabData(2).isValid());
    QCOMPARE(bar.indexOfAccount(QStringLiteral("nope")), -1);
    // And it still follows the default account through a reorder.
    bar.moveTab(0, 1);
    QCOMPARE(bar.indexOfAccount(QString()), 1);
    QCOMPARE(bar.indexOfAccount(QStringLiteral("w1")), 0);
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
// HdMedia: the injected HD-default observer toggles with the setting.
class TstHdMedia : public QObject {
  Q_OBJECT
private slots:
  void offAndOn() {
    HdMedia::setEnabled(false);
    QVERIFY(HdMedia::scriptSource().contains(QLatin1String("if (!false) return")) ||
            HdMedia::scriptSource().contains(QLatin1String("!false")));
    HdMedia::setEnabled(true);
    const QString js = HdMedia::scriptSource();
    QVERIFY(js.contains(QLatin1String("MutationObserver")));
    QVERIFY(js.contains(QLatin1String("HD")));
    QVERIFY(js.contains(QLatin1String("catch")));       // never breaks the page
    QVERIFY(js.contains(QLatin1String("disconnect")));   // re-runnable
    // #34: HD is enabled at most once per editor session (a `tried` guard reset
    // only when the editor closes), so a "not HD resolution" rejection on
    // sub-HD media cannot re-open the dialog forever.
    QVERIFY(js.contains(QLatin1String("tried")));
    // #96: the control is identified by the name inside WhatsApp's own icon,
    // which is not translated and does not change with the media. Its label is
    // both — "Photo quality" when HD is possible, the reason it is not when it
    // is not — so a label search for "HD" matched this control only while it
    // was refusing: HD was never enabled for media that could take it, and
    // every attachment that could not cost a dialog.
    QVERIFY(js.contains(QLatin1String("wds-ic-hd-settings")));
    QVERIFY(js.contains(QLatin1String("data-testid")));
    QVERIFY(!js.contains(QLatin1String("button[aria-label]")));
    // Whether HD is possible is settled before anything is clicked, so the
    // "Cannot set to HD" dialog is never provoked at all. WhatsApp dims the icon
    // when it is impossible — the control itself is identical either way, down to
    // aria-disabled="false" — so the rendered opacity is what is read.
    QVERIFY(js.contains(QLatin1String("getComputedStyle")));
    QVERIFY(js.contains(QLatin1String("opacity")));
    // The dimming comes from a generated class name that changes between
    // WhatsApp deployments; the effect is read, never the name.
    QVERIFY(!js.contains(QLatin1String("xyd83as")));
    // And if WhatsApp ever stops dimming it, a refusal that does get through is
    // remembered by its wording rather than provoked again for every attachment.
    QVERIFY(js.contains(QLatin1String("refused")));
    QVERIFY(js.contains(QLatin1String("[role=\"dialog\"]")));
    // A menu entry is only taken from outside the conversation, so a message
    // that merely says "HD" cannot be clicked instead.
    QVERIFY(js.contains(QLatin1String("closest('#main')")));
    HdMedia::setEnabled(false);
  }
  void installOnProfile() {
    HdMedia::setEnabled(true);
    QWebEngineProfile profile(QStringLiteral("tst_hd"));
    HdMedia::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-hd-media")).size(), 1);
    HdMedia::setEnabled(false);
    HdMedia::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-hd-media")).size(), 0);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// UndoSend (#7): the injected Enter-hold script tracks the setting and delay.
class TstUndoSend : public QObject {
  Q_OBJECT
private slots:
  void defaultsAndClamp() {
    // Opt-in: off until the user turns it on.
    UndoSend::setEnabled(false);
    QVERIFY(!UndoSend::isEnabled());
    UndoSend::setEnabled(true);
    QVERIFY(UndoSend::isEnabled());
    UndoSend::setSeconds(0);              // clamped to a sane minimum
    QVERIFY(UndoSend::seconds() >= 1);
    UndoSend::setSeconds(8);
    QCOMPARE(UndoSend::seconds(), 8);
  }
  void scriptReflectsState() {
    UndoSend::setEnabled(false);
    QVERIFY(UndoSend::scriptSource().contains(QLatin1String("enabled: false")));
    UndoSend::setEnabled(true);
    UndoSend::setSeconds(6);
    const QString js = UndoSend::scriptSource();
    QVERIFY(js.contains(QLatin1String("enabled: true")));
    QVERIFY(js.contains(QLatin1String("secs: 6")));
    // Capture-phase Enter interceptor that can hold and then re-send.
    QVERIFY(js.contains(QLatin1String("keydown")));
    QVERIFY(js.contains(QLatin1String("preventDefault")));
    QVERIFY(js.contains(QLatin1String("stopImmediatePropagation")));
    QVERIFY(js.contains(QLatin1String("bypass")));       // avoids a resend loop
    QVERIFY(js.contains(QLatin1String("catch")));        // never breaks the page
    UndoSend::setEnabled(false);
  }
  void installOnProfile() {
    UndoSend::setEnabled(true);
    QWebEngineProfile profile(QStringLiteral("tst_undo"));
    UndoSend::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-undo-send")).size(), 1);
    // Re-installing replaces rather than stacks the script.
    UndoSend::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-undo-send")).size(), 1);
    UndoSend::setEnabled(false);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Translator (#6): pure request/response/target-language and script helpers.
class TstTranslator : public QObject {
  Q_OBJECT
private slots:
  void targetLanguageResolution() {
    // A configured code always wins, lower-cased.
    QCOMPARE(Translate::effectiveTargetLang(QStringLiteral("FR"),
                                            QStringLiteral("es_ES")),
             QStringLiteral("fr"));
    // Otherwise the base of the app language, region stripped.
    QCOMPARE(Translate::effectiveTargetLang(QString(), QStringLiteral("es_ES")),
             QStringLiteral("es"));
    QCOMPARE(Translate::effectiveTargetLang(QString(), QStringLiteral("pt-BR")),
             QStringLiteral("pt"));
    QCOMPARE(Translate::effectiveTargetLang(QString(), QStringLiteral("de")),
             QStringLiteral("de"));
    // Nothing usable falls back to English.
    QCOMPARE(Translate::effectiveTargetLang(QString(), QString()),
             QStringLiteral("en"));
  }
  void requestBody() {
    const QByteArray b = Translate::buildRequestBody(
        QStringLiteral("hola"), QString(), QStringLiteral("en"),
        QStringLiteral("k3y"));
    const QJsonObject o = QJsonDocument::fromJson(b).object();
    QCOMPARE(o.value("q").toString(), QStringLiteral("hola"));
    QCOMPARE(o.value("source").toString(), QStringLiteral("auto")); // auto-detect
    QCOMPARE(o.value("target").toString(), QStringLiteral("en"));
    QCOMPARE(o.value("format").toString(), QStringLiteral("text"));
    QCOMPARE(o.value("api_key").toString(), QStringLiteral("k3y"));
    // An empty key is omitted entirely, not sent blank.
    const QJsonObject o2 =
        QJsonDocument::fromJson(Translate::buildRequestBody(
                                    QStringLiteral("x"), QStringLiteral("es"),
                                    QStringLiteral("en"), QString()))
            .object();
    QVERIFY(!o2.contains(QStringLiteral("api_key")));
    QCOMPARE(o2.value("source").toString(), QStringLiteral("es"));
  }
  void responseParsing() {
    QString err;
    QCOMPARE(Translate::parseResponse(
                 QByteArray("{\"translatedText\":\"hello\"}"), &err),
             QStringLiteral("hello"));
    QVERIFY(err.isEmpty());
    // LibreTranslate error object surfaces its message and no text.
    QVERIFY(Translate::parseResponse(QByteArray("{\"error\":\"bad key\"}"), &err)
                .isEmpty());
    QCOMPARE(err, QStringLiteral("bad key"));
    // Garbage never crashes; it reports an error.
    QVERIFY(Translate::parseResponse(QByteArray("not json"), &err).isEmpty());
    QVERIFY(!err.isEmpty());
  }
  void scriptsAreSafe() {
    // Composer replace and toast must JSON-escape the payload and never throw.
    const QString rep = Translate::replaceComposerScript(
        QStringLiteral("say \"hi\"\nline2 \\ end"));
    QVERIFY(rep.contains(QLatin1String("execCommand")));
    QVERIFY(rep.contains(QLatin1String("\\\"")));
    QVERIFY(rep.contains(QLatin1String("\\n")));
    QVERIFY(rep.contains(QLatin1String("catch")));
    const QString toast = Translate::toastScript(QStringLiteral("¡hola \"tú\"!"));
    QVERIFY(toast.contains(QLatin1String("whatly-translate-toast")));
    QVERIFY(toast.contains(QLatin1String("\\\"")));
    QVERIFY(toast.contains(QLatin1String("catch")));
    QVERIFY(Translate::readSelectionScript().contains(
        QLatin1String("getSelection")));
    QVERIFY(Translate::readComposerScript().contains(
        QLatin1String("contenteditable")));
  }
  void settingsRoundTrip() {
    Translate::setEnabled(true);
    QVERIFY(Translate::isEnabled());
    Translate::setEndpoint(QStringLiteral("  https://lt.example/translate  "));
    QCOMPARE(Translate::endpoint(),
             QStringLiteral("https://lt.example/translate")); // trimmed
    Translate::setTargetLang(QStringLiteral("fr"));
    QCOMPARE(Translate::targetLang(), QStringLiteral("fr"));
    Translate::setEnabled(false);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ChatExport (#9): parsing the collected payload, media decoding, and the
// transcript/JSON writers.
class TstChatExport : public QObject {
  Q_OBJECT
private slots:
  void mimeAndSanitize() {
    QCOMPARE(ChatExport::extForMime("image/jpeg"), QStringLiteral(".jpg"));
    QCOMPARE(ChatExport::extForMime("image/png"), QStringLiteral(".png"));
    QCOMPARE(ChatExport::extForMime("video/mp4"), QStringLiteral(".mp4"));
    QCOMPARE(ChatExport::extForMime("application/octet-stream"),
             QStringLiteral(".bin"));
    QCOMPARE(ChatExport::sanitizeFileName("a/b:c*?\"<>|d"),
             QStringLiteral("a_b_c______d")); // / : * ? " < > | -> one _ each
    QCOMPARE(ChatExport::sanitizeFileName("   "), QStringLiteral("chat"));
  }
  void decodesDataUrl() {
    QString mime;
    // "hi" base64 is "aGk=".
    const QByteArray b =
        ChatExport::decodeDataUrl("data:image/png;base64,aGk=", &mime);
    QCOMPARE(mime, QStringLiteral("image/png"));
    QCOMPARE(b, QByteArray("hi"));
    QVERIFY(ChatExport::decodeDataUrl("not a data url", &mime).isEmpty());
  }
  void parsesMessagesAndMedia() {
    QJsonArray arr;
    arr.append(QJsonObject{{"ts", "08:49, 2/8/2026"},
                           {"sender", "Ana"},
                           {"dir", "in"},
                           {"type", "text"},
                           {"text", "hola"}});
    arr.append(QJsonObject{
        {"ts", "08:50, 2/8/2026"},
        {"sender", ""},
        {"dir", "out"},
        {"type", "image"},
        {"text", "caption"},
        {"media", QJsonObject{{"dataUrl", "data:image/jpeg;base64,aGk="},
                              {"mime", "image/jpeg"}}}});
    arr.append(QJsonObject{{"ts", "08:51, 2/8/2026"},
                           {"sender", ""},
                           {"dir", "in"},
                           {"type", "video"},
                           {"text", ""}}); // media not downloaded

    QHash<QString, QByteArray> media;
    const QList<ChatExport::Message> msgs = ChatExport::parse(arr, &media);
    QCOMPARE(msgs.size(), 3);
    QCOMPARE(media.size(), 1);
    QCOMPARE(msgs[1].mediaFile, QStringLiteral("0001.jpg"));
    QCOMPARE(media.value("0001.jpg"), QByteArray("hi"));
    QVERIFY(!msgs[0].mediaMissing);
    QVERIFY(msgs[2].mediaMissing); // video with no bytes

    // Transcript: WhatsApp-style lines, media reference, and "You" for an
    // outgoing message that had no sender name.
    const QString txt = ChatExport::buildTranscript("Ana", msgs);
    QVERIFY(txt.contains(QLatin1String("[08:49, 2/8/2026] Ana: hola")));
    QVERIFY(txt.contains(QLatin1String("[08:50, 2/8/2026] You: <attached: media/0001.jpg> caption")));
    QVERIFY(txt.contains(QLatin1String("<video omitted>")));

    // JSON: media path present for the downloaded one, null for the missing one.
    const QJsonArray out =
        QJsonDocument::fromJson(ChatExport::buildJson(msgs)).array();
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[1].toObject().value("media").toString(),
             QStringLiteral("media/0001.jpg"));
    QVERIFY(out[2].toObject().value("media").isNull());
  }
  void scriptsWellFormed() {
    const QString col = ChatExport::collectorScript();
    QVERIFY(col.contains(QLatin1String("__whatlyExport")));
    QVERIFY(col.contains(QLatin1String("data-pre-plain-text")));
    QVERIFY(col.contains(QLatin1String("scrollTop")));   // auto-scroll
    QVERIFY(col.contains(QLatin1String("readAsDataURL"))); // media capture
    QVERIFY(col.contains(QLatin1String("catch")));
    QVERIFY(ChatExport::statusScript().contains(QLatin1String("status")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// AiAssistant (#5): OpenAI-compatible request building, response parsing,
// prompts, the context script and settings.
class TstAiAssistant : public QObject {
  Q_OBJECT
private slots:
  void requestBody() {
    const QByteArray b = Ai::buildChatRequest(
        QStringLiteral("gpt-4o-mini"), QStringLiteral("sys"),
        QStringLiteral("hello"));
    const QJsonObject o = QJsonDocument::fromJson(b).object();
    QCOMPARE(o.value("model").toString(), QStringLiteral("gpt-4o-mini"));
    const QJsonArray msgs = o.value("messages").toArray();
    QCOMPARE(msgs.size(), 2);
    QCOMPARE(msgs[0].toObject().value("role").toString(),
             QStringLiteral("system"));
    QCOMPARE(msgs[0].toObject().value("content").toString(),
             QStringLiteral("sys"));
    QCOMPARE(msgs[1].toObject().value("role").toString(),
             QStringLiteral("user"));
    QCOMPARE(msgs[1].toObject().value("content").toString(),
             QStringLiteral("hello"));
  }
  void responseParsing() {
    QString err;
    const QByteArray ok =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\","
        "\"content\":\"  hi there  \"}}]}";
    QCOMPARE(Ai::parseChatResponse(ok, &err), QStringLiteral("hi there"));
    QVERIFY(err.isEmpty());
    // OpenAI nested error object.
    QVERIFY(Ai::parseChatResponse(
                "{\"error\":{\"message\":\"bad key\"}}", &err).isEmpty());
    QCOMPARE(err, QStringLiteral("bad key"));
    // Garbage never crashes.
    QVERIFY(Ai::parseChatResponse("not json", &err).isEmpty());
    QVERIFY(!err.isEmpty());
  }
  void promptsAndContext() {
    QVERIFY(!Ai::summarizeSystemPrompt().isEmpty());
    QVERIFY(!Ai::improveSystemPrompt().isEmpty());
    QVERIFY(!Ai::suggestReplySystemPrompt().isEmpty());
    QVERIFY(!Ai::unreadDigestSystemPrompt().isEmpty());
    QVERIFY(!Ai::rewriteFormalSystemPrompt().isEmpty());
    QVERIFY(!Ai::rewriteFriendlySystemPrompt().isEmpty());
    QVERIFY(!Ai::rewriteShorterSystemPrompt().isEmpty());
    const QString js = Ai::readContextScript(50);
    QVERIFY(js.contains(QLatin1String("data-pre-plain-text")));
    QVERIFY(js.contains(QLatin1String("selectable-text")));
    QVERIFY(js.contains(QLatin1String("slice(-50)")));
    QVERIFY(js.contains(QLatin1String("catch")));
  }
  void unreadDigestInput() {
    const QString in = Ai::buildUnreadDigestInput(QStringLiteral(
        "[{\"name\":\"Ana\",\"count\":3,\"preview\":\"dinner tonight?\"},"
        "{\"name\":\"Work\",\"count\":12,\"preview\":\"\"},"
        "{\"name\":\"\",\"count\":1,\"preview\":\"skip me\"}]"));
    const QStringList lines = in.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 2); // the nameless entry is dropped
    QCOMPARE(lines.at(0), QStringLiteral("Ana (3 unread): dinner tonight?"));
    QCOMPARE(lines.at(1), QStringLiteral("Work (12 unread)")); // no preview
    // Empty / unparseable input yields nothing rather than crashing.
    QVERIFY(Ai::buildUnreadDigestInput(QStringLiteral("[]")).isEmpty());
    QVERIFY(Ai::buildUnreadDigestInput(QStringLiteral("not json")).isEmpty());
    QVERIFY(Ai::buildUnreadDigestInput(QString()).isEmpty());
  }
  void settingsRoundTrip() {
    Ai::setEnabled(true);
    QVERIFY(Ai::isEnabled());
    Ai::setEndpoint(QStringLiteral("  http://localhost:11434/v1/chat/completions  "));
    QCOMPARE(Ai::endpoint(),
             QStringLiteral("http://localhost:11434/v1/chat/completions"));
    Ai::setModel(QStringLiteral("llama3"));
    QCOMPARE(Ai::model(), QStringLiteral("llama3"));
    Ai::setEnabled(false);
  }
  void memAvailableParsing() {
    const QByteArray mi =
        "MemTotal:       30412860 kB\n"
        "MemFree:          361234 kB\n"
        "MemAvailable:    2097152 kB\n"
        "Buffers:           12345 kB\n";
    QCOMPARE(Ai::memAvailableMbFromProc(mi), 2048L); // 2097152 kB -> 2048 MiB
    QCOMPARE(Ai::memAvailableMbFromProc("MemTotal: 100 kB\n"), -1L);
    QCOMPARE(Ai::memAvailableMbFromProc(QByteArray()), -1L);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Ollama helper (#5 ease-of-use): URL derivation, tag/progress parsing, list.
class TstOllama : public QObject {
  Q_OBJECT
private slots:
  void baseUrlDerivation() {
    QCOMPARE(Ollama::baseUrl(
                 QStringLiteral("http://localhost:11434/v1/chat/completions")),
             QStringLiteral("http://localhost:11434"));
    QCOMPARE(Ollama::baseUrl(QStringLiteral("https://api.openai.com/v1/chat/completions")),
             QStringLiteral("https://api.openai.com"));
    // Empty / unparseable falls back to the default local Ollama.
    QCOMPARE(Ollama::baseUrl(QString()),
             QStringLiteral("http://localhost:11434"));
  }
  void localDetection() {
    QVERIFY(Ollama::isLocalEndpoint(
        QStringLiteral("http://localhost:11434/v1/chat/completions")));
    QVERIFY(Ollama::isLocalEndpoint(
        QStringLiteral("http://127.0.0.1:11434/v1")));
    QVERIFY(!Ollama::isLocalEndpoint(
        QStringLiteral("https://api.openai.com/v1/chat/completions")));
  }
  void installedModelsParsing() {
    const QByteArray json =
        "{\"models\":[{\"name\":\"qwen2.5:3b\"},{\"name\":\"llama3.2:1b\"}]}";
    const QStringList m = Ollama::parseInstalledModels(json);
    QCOMPARE(m.size(), 2);
    QCOMPARE(m.first(), QStringLiteral("qwen2.5:3b"));
    QVERIFY(Ollama::parseInstalledModels("not json").isEmpty());
  }
  void pullProgressParsing() {
    QString status;
    QCOMPARE(Ollama::parsePullProgress(
                 "{\"status\":\"pulling\",\"total\":100,\"completed\":25}",
                 &status),
             25);
    QCOMPARE(status, QStringLiteral("pulling"));
    // No total yet -> unknown percent, but status still surfaces.
    QCOMPARE(Ollama::parsePullProgress("{\"status\":\"verifying\"}", &status),
             -1);
    QCOMPARE(status, QStringLiteral("verifying"));
  }
  void recommendedListIsLight() {
    const auto list = Ollama::recommendedModels();
    QVERIFY(list.size() >= 3);
    for (const auto &m : list)
      QVERIFY(!m.name.isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// PassLock (#42): App Lock passcode is stored as a salted hash, verified by
// hashing, and legacy Base64 values still verify (then get upgraded).
class TstPassLock : public QObject {
  Q_OBJECT
private slots:
  void hashIsSaltedAndNotReversible() {
    const QString h = PassLock::hash(QStringLiteral("s3cret!"));
    QVERIFY(PassLock::isHashed(h));
    QVERIFY(h.startsWith(QLatin1String("pbkdf2_sha256$")));
    // The plaintext must not appear anywhere in the stored value.
    QVERIFY(!h.contains(QLatin1String("s3cret!")));
    // A different call uses a fresh salt, so the stored strings differ...
    const QString h2 = PassLock::hash(QStringLiteral("s3cret!"));
    QVERIFY(h != h2);
    // ...yet both verify the same passcode.
    QVERIFY(PassLock::verify(QStringLiteral("s3cret!"), h));
    QVERIFY(PassLock::verify(QStringLiteral("s3cret!"), h2));
  }
  void verifyRejectsWrong() {
    const QString h = PassLock::hash(QStringLiteral("correct horse"));
    QVERIFY(!PassLock::verify(QStringLiteral("wrong"), h));
    QVERIFY(!PassLock::verify(QString(), h));
    QVERIFY(!PassLock::verify(QStringLiteral("correct horse "), h)); // exact
  }
  void legacyBase64StillVerifies() {
    // Old format: Base64 of the plaintext.
    const QString legacy =
        QString::fromLatin1(QByteArray("1234").toBase64());
    QVERIFY(!PassLock::isHashed(legacy)); // so the caller upgrades it
    QVERIFY(PassLock::verify(QStringLiteral("1234"), legacy));
    QVERIFY(!PassLock::verify(QStringLiteral("0000"), legacy));
  }
  void malformedHashDoesNotVerify() {
    QVERIFY(!PassLock::verify(QStringLiteral("x"),
                             QStringLiteral("pbkdf2_sha256$bad")));
    QVERIFY(!PassLock::verify(QStringLiteral("x"), QString()));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// CannedResponses: CRUD round-trip and the escaped insert snippet.
class TstCannedResponses : public QObject {
  Q_OBJECT
private slots:
  void init() { CannedResponses::setAll({}); }
  void cleanup() { CannedResponses::setAll({}); }

  void addListRemove() {
    QVERIFY(CannedResponses::all().isEmpty());
    CannedResponses::add(QStringLiteral("Greeting"), QStringLiteral("Hi!"));
    CannedResponses::add(QStringLiteral("Bye"), QStringLiteral("Talk later"));
    auto list = CannedResponses::all();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.first().title, QStringLiteral("Greeting"));
    QCOMPARE(list.first().text, QStringLiteral("Hi!"));
    CannedResponses::removeAt(0);
    QCOMPARE(CannedResponses::all().size(), 1);
    QCOMPARE(CannedResponses::all().first().title, QStringLiteral("Bye"));
    CannedResponses::removeAt(99); // out of range is a no-op
    QCOMPARE(CannedResponses::all().size(), 1);
  }

  void insertScriptEscapesText() {
    const QString js = CannedResponses::insertScript(
        QStringLiteral("say \"hi\"\nand \\ bye"));
    QVERIFY(js.contains(QLatin1String("insertText")));
    QVERIFY(js.contains(QLatin1String("catch")));
    // Quotes, newlines and backslashes must be escaped, not raw.
    QVERIFY(js.contains(QLatin1String("\\\"")));
    QVERIFY(js.contains(QLatin1String("\\n")));
    QVERIFY(!js.contains(QLatin1String("say \"hi\"\n")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// FocusMode: the chat-list masking stylesheet toggles with the setting.
class TstFocusMode : public QObject {
  Q_OBJECT
private slots:
  void offProducesNoStyle() {
    FocusMode::setEnabled(false);
    QVERIFY(!FocusMode::isEnabled());
    const QString js = FocusMode::scriptSource();
    QVERIFY(js.contains(QLatin1String("var ON = false")));
  }
  void onMasksTheChatList() {
    FocusMode::setEnabled(true);
    QVERIFY(FocusMode::isEnabled());
    const QString js = FocusMode::scriptSource();
    QVERIFY(js.contains(QLatin1String("var ON = true")));
    QVERIFY(js.contains(QLatin1String("pane-side")));   // the chat list
    QVERIFY(js.contains(QLatin1String("blur(")));       // masked, not removed
    QVERIFY(js.contains(QLatin1String(":hover")));      // hover reveals one
    QVERIFY(js.contains(QLatin1String("catch")));       // never breaks the page
    FocusMode::setEnabled(false);
  }
  void installOnProfile() {
    FocusMode::setEnabled(true);
    QWebEngineProfile profile(QStringLiteral("tst_focus"));
    FocusMode::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-focus-mode")).size(), 1);
    FocusMode::setEnabled(false);
    FocusMode::install(&profile);
    QCOMPARE(profile.scripts()->find(QStringLiteral("whatly-focus-mode")).size(), 0);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// QuickReply: the composer-focus snippet injected when a notification is opened.
class TstQuickReply : public QObject {
  Q_OBJECT
private slots:
  void scriptIsGuardedAndTargetsComposer() {
    const QString js = QuickReply::focusComposerScript();
    QVERIFY(!js.isEmpty());
    // Wrapped in an IIFE with a try/catch so it can never break the page.
    QVERIFY(js.contains(QLatin1String("try")));
    QVERIFY(js.contains(QLatin1String("catch")));
    // Targets the footer composer and focuses it.
    QVERIFY(js.contains(QLatin1String("footer")));
    QVERIFY(js.contains(QLatin1String("contenteditable")));
    QVERIFY(js.contains(QLatin1String(".focus()")));
    // Gives up rather than polling forever.
    QVERIFY(js.contains(QLatin1String("clearInterval")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ScreenLock: the lock-on-session-lock decision.
class TstScreenLock : public QObject {
  Q_OBJECT
private slots:
  void decision() {
    ScreenLock::setEnabled(true);
    QVERIFY(ScreenLock::shouldLock(true, true));    // active + configured + on
    QVERIFY(!ScreenLock::shouldLock(false, true));  // saver not active
    QVERIFY(!ScreenLock::shouldLock(true, false));  // no passcode
    ScreenLock::setEnabled(false);
    QVERIFY(!ScreenLock::shouldLock(true, true));   // feature off
  }
  void settingRoundTrips() {
    ScreenLock::setEnabled(true);
    QVERIFY(ScreenLock::isEnabled());
    ScreenLock::setEnabled(false);
    QVERIFY(!ScreenLock::isEnabled());
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
  // Which advice the update notification gives depends on this, so every case
  // is pinned rather than left to whichever branch happens to run.
  void recognisesTheInstallFlavour() {
    using I = UpdateCheck::Install;
    // A Flatpak wins over everything else: inside the sandbox the paths of the
    // other cases are meaningless.
    QCOMPARE(UpdateCheck::installFrom(true, QString(), "/app/bin/whatly"),
             I::Flatpak);
    QCOMPARE(UpdateCheck::installFrom(true, "/tmp/x.AppImage", "/usr/bin/whatly"),
             I::Flatpak);
    // $APPIMAGE set is what marks a running AppImage, whatever it mounted at.
    QCOMPARE(UpdateCheck::installFrom(false, "/home/u/Whatly.AppImage",
                                      "/tmp/.mount_abc/usr/bin/whatly"),
             I::AppImage);
    // /usr means a package the distribution owns and updates.
    QCOMPARE(UpdateCheck::installFrom(false, QString(), "/usr/bin/whatly"),
             I::DistroPackage);
    // Our own .deb and .rpm bundle their Qt under /opt, and their user does
    // fetch the next build by hand, so they must not read as distro-owned.
    QCOMPARE(UpdateCheck::installFrom(false, QString(), "/opt/whatly/bin/whatly"),
             I::Unknown);
    // Windows, macOS and a portable archive have none of the three markers.
    QCOMPARE(UpdateCheck::installFrom(
                 false, QString(), "C:/Program Files/Whatly/whatly.exe"),
             I::Unknown);
    QCOMPARE(UpdateCheck::installFrom(false, QString(),
                                      "/home/u/whatly-portable/whatly"),
             I::Unknown);
    // "/usr" must match as a directory, not as a prefix of another name.
    QCOMPARE(UpdateCheck::installFrom(false, QString(), "/usrlocal/whatly"),
             I::Unknown);
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
    NotificationRules::setVipContacts({});
    NotificationRules::setMutedContacts({});
    NotificationRules::dndOff();
  }

  // Per-contact profiles (#10): VIP breaks DND, muted is always silenced and
  // wins over VIP/keywords.
  void contactProfiles() {
    const QDateTime night(QDate(2026, 1, 1), QTime(23, 0)); // inside DND
    NotificationRules::setDndEnabled(true);

    // VIP notifies even during DND.
    NotificationRules::setVipContacts({QStringLiteral("Alice")});
    QVERIFY(NotificationRules::shouldNotify(night, QStringLiteral("Alice"),
                                            QStringLiteral("hi")));
    // A non-VIP is suppressed during DND.
    QVERIFY(!NotificationRules::shouldNotify(night, QStringLiteral("Bob"),
                                             QStringLiteral("hi")));

    // Muted is always silenced, even outside DND and even if also VIP/keyword.
    NotificationRules::setDndEnabled(false);
    NotificationRules::setMutedContacts({QStringLiteral("Noisy group")});
    NotificationRules::setKeywords({QStringLiteral("hi")});
    NotificationRules::setVipContacts({QStringLiteral("Noisy group")});
    const QDateTime day(QDate(2026, 1, 1), QTime(12, 0));
    QVERIFY(!NotificationRules::shouldNotify(
        day, QStringLiteral("Noisy group"), QStringLiteral("hi")));

    // Case-insensitive contains match.
    QVERIFY(NotificationRules::matchesContact({QStringLiteral("alice")},
                                              QStringLiteral("Alice (2)")));
    QVERIFY(!NotificationRules::matchesContact({QStringLiteral("Carol")},
                                               QStringLiteral("Alice")));
  }

  void inlineReplySetting() {
    // Default on (idea #2); it only takes effect where the backend supports it.
    NotificationRules::settings().remove(QStringLiteral("notif/inlineReply"));
    QVERIFY(NotificationRules::inlineReplyEnabled());
    NotificationRules::setInlineReplyEnabled(false);
    QVERIFY(!NotificationRules::inlineReplyEnabled());
    NotificationRules::setInlineReplyEnabled(true);
    QVERIFY(NotificationRules::inlineReplyEnabled());
  }

  void inlineReplyCapabilityDetection() {
    QVERIFY(NotificationReplyUtil::hasInlineReply(
        {QStringLiteral("body"), QStringLiteral("actions"),
         QStringLiteral("inline-reply")}));
    QVERIFY(!NotificationReplyUtil::hasInlineReply(
        {QStringLiteral("body"), QStringLiteral("actions")}));
    QVERIFY(!NotificationReplyUtil::hasInlineReply({}));
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

  // Manual DND (#10): on demand, independent of the schedule, VIP still breaks.
  void manualDndOnDemand() {
    const QDateTime now(QDate(2026, 1, 1), QTime(12, 0)); // midday, no schedule
    // Pure core.
    QVERIFY(NotificationRules::manualActive(true, QDateTime(), now));       // indefinite
    QVERIFY(NotificationRules::manualActive(false, now.addSecs(60), now));  // until future
    QVERIFY(!NotificationRules::manualActive(false, now.addSecs(-60), now));// expired
    QVERIFY(!NotificationRules::manualActive(false, QDateTime(), now));     // off
    // Indefinite, through settings + shouldNotify.
    NotificationRules::dndOnIndefinite();
    QVERIFY(NotificationRules::manualDndActive(now));
    QVERIFY(!NotificationRules::shouldNotify(now, "Ana", "hi"));
    // VIP still breaks through manual DND.
    NotificationRules::setVipContacts({QStringLiteral("Ana")});
    QVERIFY(NotificationRules::shouldNotify(now, "Ana", "hi"));
    NotificationRules::setVipContacts({});
    // A timed snooze already in the past is not active.
    NotificationRules::dndSnoozeUntil(now.addSecs(-1));
    QVERIFY(!NotificationRules::manualDndActive(now));
    QVERIFY(NotificationRules::shouldNotify(now, "Ana", "hi"));
    // Turning it off clears both.
    NotificationRules::dndOnIndefinite();
    NotificationRules::dndOff();
    QVERIFY(!NotificationRules::manualDndActive(now));
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

  // A manual mode with no host must not install an unusable proxy: doing so
  // fails every request with ERR_NO_SUPPORTED_PROXIES, which looks like the
  // network is down rather than like a settings problem.
  void applyManualWithoutHostGivesNoProxy() {
    NetworkProxy::setMode(QStringLiteral("http"));
    NetworkProxy::setHost(QString());
    NetworkProxy::setPort(65535);
    NetworkProxy::applyToApplication();
    QCOMPARE(QNetworkProxy::applicationProxy().type(), QNetworkProxy::NoProxy);

    // Same for socks5, and for a host that is only whitespace.
    NetworkProxy::setMode(QStringLiteral("socks5"));
    NetworkProxy::setHost(QStringLiteral("   "));
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
    ChatListStrip::setCollapsed(true);
    ChatListStrip::install(&profile);
    ChatWallpaper::install(&profile);
    CustomCss::install(&profile);
    WebTweaks::install(&profile);
    LinkedDeviceName::install(&profile, QStringLiteral("Work"));
    QVERIFY(profile.scripts()->count() > before);

    // #7: WebTweaks::install() always registers the HD receive-flag script,
    // independent of any user tweak.
    QVERIFY(!profile.scripts()
                 ->find(QStringLiteral("whatly-hd-media-flag"))
                 .isEmpty());

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
    ChatListStrip::setCollapsed(false);
    ChatListStrip::install(&profile);
    ChatWallpaper::install(&profile);
    CustomCss::install(&profile);
  }
};

// Guards the MSVC single-string-literal cap (C2026, 16380 bytes). The injected
// scripts are large raw string literals; MSVC — unlike GCC/Clang — rejects any
// single literal past the cap, which silently breaks only the Windows build. This
// walks the source and fails if any raw string literal has grown past it, so the
// regression is caught in the suite everyone runs rather than at Windows
// packaging time. The fix is always to split the literal into adjacent literals
// at a statement boundary; the compiler concatenates them, so the string is
// unchanged (see #64).
class TstScriptLiterals : public QObject {
  Q_OBJECT
private slots:
  void rawLiteralsUnderMsvcCap() {
    static constexpr int kMsvcCap = 16380;
    const QString root = QStringLiteral(WHATLY_SOURCE_DIR);
    const QString srcDir = root + QStringLiteral("/src");
    QVERIFY2(QDir(srcDir).exists(), qPrintable(srcDir));

    // R"delim( ... )delim": the backreference \1 ties the closing delimiter to
    // the opening one, and DotMatchesEverything lets a literal span many lines.
    // Group 2 is the literal's content, whose byte length is what MSVC limits.
    QRegularExpression rx(QStringLiteral(R"(R"([A-Za-z0-9_]*)\((.*?)\)\1")"),
                          QRegularExpression::DotMatchesEverythingOption);
    QVERIFY(rx.isValid());

    int filesScanned = 0, literalsChecked = 0;
    QDirIterator it(srcDir,
                    {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      // Skip vendored third-party sources: not ours to reshape.
      if (path.contains(QStringLiteral("/libnotify-qt/")) ||
          path.contains(QStringLiteral("/singleapplication/")))
        continue;
      QFile f(path);
      QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path));
      const QString text = QString::fromUtf8(f.readAll());
      ++filesScanned;
      auto matches = rx.globalMatch(text);
      while (matches.hasNext()) {
        const int len = matches.next().captured(2).toUtf8().size();
        ++literalsChecked;
        QVERIFY2(len < kMsvcCap,
                 qPrintable(
                     QStringLiteral("%1: a raw string literal is %2 bytes, over "
                                    "the MSVC %3-byte cap (C2026). Split it into "
                                    "adjacent literals at a statement boundary "
                                    "(see #64).")
                         .arg(QDir(root).relativeFilePath(path))
                         .arg(len)
                         .arg(kMsvcCap)));
      }
    }
    // Sanity: the scan must actually reach the source and find literals, or a
    // wrong path would make this test silently vacuous.
    QVERIFY(filesScanned > 0);
    QVERIFY(literalsChecked > 0);
  }
};

class TstDictionaryManager : public QObject {
  Q_OBJECT
private slots:
  void parseManifest() {
    const QByteArray json = R"({"dictionaries":[
      {"code":"en_US","size":5,"sha256":"ABC"},
      {"code":"es_ES","size":10,"sha256":"def"},
      {"size":3,"sha256":"nope"}
    ]})";
    const auto list = DictionaryManager::parseManifest(json);
    QCOMPARE(list.size(), 2); // the entry without a code is skipped
    QCOMPARE(list[0].code, QStringLiteral("en_US"));
    QCOMPARE(list[0].size, qint64(5));
    QCOMPARE(list[0].sha256, QStringLiteral("abc")); // lower-cased
    // Junk / non-object is tolerated, not a crash.
    QVERIFY(DictionaryManager::parseManifest("not json").isEmpty());
    QVERIFY(DictionaryManager::parseManifest("[]").isEmpty());
  }
  void verify() {
    const QByteArray data = QByteArrayLiteral("hello");
    const QString sha = QStringLiteral(
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    QVERIFY(DictionaryManager::verify(data, 5, sha));
    QVERIFY(DictionaryManager::verify(data, 5, sha.toUpper())); // case-insensitive
    QVERIFY(DictionaryManager::verify(data, 0, sha));           // size 0 = skip size check
    QVERIFY(!DictionaryManager::verify(data, 4, sha));          // wrong size
    QVERIFY(!DictionaryManager::verify(data, 5, QStringLiteral("deadbeef"))); // wrong hash
    QVERIFY(!DictionaryManager::verify(data, 5, QString()));    // unverifiable -> not trusted
  }
  void displayName() {
    // A recognised locale reads as its native name, not the raw code.
    const QString de = DictionaryManager::displayName(QStringLiteral("de_DE"));
    QVERIFY(!de.isEmpty());
    QVERIFY(de != QStringLiteral("de_DE"));
    // An unrecognised code falls back to itself rather than "C"/empty.
    QCOMPARE(DictionaryManager::displayName(QStringLiteral("zz_ZZ")),
             QStringLiteral("zz_ZZ"));
  }
  void urls() {
    QVERIFY(DictionaryManager::manifestUrl().endsWith(QStringLiteral("/manifest.json")));
    QVERIFY(DictionaryManager::assetUrl(QStringLiteral("en_US"))
                .endsWith(QStringLiteral("/en_US.bdic")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// SessionBackup: snapshot/restore of a linked session so a wiped IndexedDB does
// not force a re-link (#43). Pure helpers plus a round trip against a temp root.
class TstSessionBackup : public QObject {
  Q_OBJECT
  // Write `bytes` bytes into <dir>/IndexedDB/db so its directory size crosses
  // (or stays under) the "session present" threshold.
  static void writeIndexedDb(const QString &profileDir, qint64 bytes) {
    const QString idb = profileDir + QStringLiteral("/IndexedDB");
    QDir().mkpath(idb);
    QFile f(idb + QStringLiteral("/db.ldb"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(int(bytes), 'x'));
    f.close();
  }
private slots:
  void cleanup() { SessionBackup::setPathsForTesting(QString(), QString()); }

  void engineSubdirMatchesProfileManager() {
    QCOMPARE(SessionBackup::engineSubdir(QString(), QString()),
             QStringLiteral("/QtWebEngine"));
    QCOMPARE(SessionBackup::engineSubdir(QString(), QStringLiteral("work")),
             QStringLiteral("/QtWebEngine-work"));
    QCOMPARE(SessionBackup::engineSubdir(QStringLiteral("-alt"), QString()),
             QStringLiteral("/QtWebEngine-alt"));
    QCOMPARE(SessionBackup::engineSubdir(QStringLiteral("-alt"),
                                         QStringLiteral("work")),
             QStringLiteral("/QtWebEngine-alt-work"));
  }
  void snapshotKeyAndThreshold() {
    QCOMPARE(SessionBackup::snapshotKey(QString()),
             QStringLiteral("default"));
    QCOMPARE(SessionBackup::snapshotKey(QStringLiteral("work")),
             QStringLiteral("work"));
    QVERIFY(!SessionBackup::sessionLooksPresent(0));
    QVERIFY(!SessionBackup::sessionLooksPresent(1024));
    QVERIFY(SessionBackup::sessionLooksPresent(2 * 1024 * 1024));
  }
  void freeSpaceGuard() {
    // Backing up on a nearly-full disk is what corrupts the copy, so it backs
    // off below the threshold (512 MB) and proceeds with ample space.
    QVERIFY(!SessionBackup::hasEnoughFreeSpace(0));
    QVERIFY(!SessionBackup::hasEnoughFreeSpace(64LL * 1024 * 1024));  // 64 MB
    QVERIFY(SessionBackup::hasEnoughFreeSpace(4LL * 1024 * 1024 * 1024)); // 4 GB
  }
  void roundTripRestoresWipedSession() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    SessionBackup::setPathsForTesting(root.path(), QString());
    const QString live = root.path() + QStringLiteral("/QtWebEngine");

    // A healthy live session snapshots successfully.
    writeIndexedDb(live, 512 * 1024);
    QVERIFY(SessionBackup::snapshot(QString()));

    // Chromium wipes the DB: the live session now looks absent.
    QDir(live + QStringLiteral("/IndexedDB")).removeRecursively();
    QVERIFY(!SessionBackup::sessionLooksPresent(
        StorageInfo::directorySize(live + QStringLiteral("/IndexedDB"))));

    // Restore brings it back above the threshold.
    QVERIFY(SessionBackup::restore(QString()));
    QVERIFY(SessionBackup::sessionLooksPresent(
        StorageInfo::directorySize(live + QStringLiteral("/IndexedDB"))));
  }
  void wipedLiveNeverOverwritesGoodSnapshot() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    SessionBackup::setPathsForTesting(root.path(), QString());
    const QString live = root.path() + QStringLiteral("/QtWebEngine");

    writeIndexedDb(live, 512 * 1024);
    QVERIFY(SessionBackup::snapshot(QString()));

    // With the live session gone, snapshot() must refuse rather than clobber
    // the good copy with an empty one.
    QDir(live + QStringLiteral("/IndexedDB")).removeRecursively();
    QVERIFY(!SessionBackup::snapshot(QString()));
    // The good snapshot still restores.
    QVERIFY(SessionBackup::restore(QString()));
    QVERIFY(SessionBackup::sessionLooksPresent(
        StorageInfo::directorySize(live + QStringLiteral("/IndexedDB"))));
  }
  void restoreWithoutSnapshotIsANoOp() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    SessionBackup::setPathsForTesting(root.path(), QString());
    QVERIFY(!SessionBackup::restore(QString()));
  }
  void profilesDoNotShareSnapshots() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    // The main instance (empty suffix) snapshots its default account.
    SessionBackup::setPathsForTesting(root.path(), QString());
    writeIndexedDb(root.path() + QStringLiteral("/QtWebEngine"), 512 * 1024);
    QVERIFY(SessionBackup::snapshot(QString()));
    // A `--profile x` instance (suffix "-x") whose own session is empty must NOT
    // find — and restore — the main instance's snapshot: that cross-profile leak
    // is the bug. Its snapshot namespace is separate, so there is nothing to
    // restore.
    SessionBackup::setPathsForTesting(root.path(), QStringLiteral("-x"));
    QVERIFY(!SessionBackup::restore(QString()));
  }
  void runsStartupRecovery() {
    QSettings &s = SettingsManager::instance().settings();
    const QVariant savedEnabled = s.value(QStringLiteral("sessionBackup/enabled"));
    const QVariant savedIds = s.value(QStringLiteral("accounts/ids"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    SessionBackup::setPathsForTesting(root.path(), QString());
    const QString live = root.path() + QStringLiteral("/QtWebEngine");
    const QString idb = live + QStringLiteral("/IndexedDB");

    // Disabled: recovery returns straight away and restores nothing.
    s.setValue(QStringLiteral("sessionBackup/enabled"), false);
    SessionBackup::runStartupRecovery();

    // Enabled: a wiped live session with a good snapshot is put back at startup.
    s.setValue(QStringLiteral("sessionBackup/enabled"), true);
    s.setValue(QStringLiteral("accounts/ids"), QStringList{});
    writeIndexedDb(live, 512 * 1024);
    QVERIFY(SessionBackup::snapshot(QString()));
    QDir(idb).removeRecursively();
    QVERIFY(!SessionBackup::sessionLooksPresent(
        StorageInfo::directorySize(idb)));

    SessionBackup::runStartupRecovery(); // the restore branch
    QVERIFY(SessionBackup::sessionLooksPresent(
        StorageInfo::directorySize(idb)));

    // A second pass, with the session now present, takes a fresh snapshot
    // instead of restoring.
    SessionBackup::runStartupRecovery(); // the snapshot branch

    s.setValue(QStringLiteral("sessionBackup/enabled"), savedEnabled);
    s.setValue(QStringLiteral("accounts/ids"), savedIds);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The widgets, instantiated headless (offscreen) and driven through their public
// surface: constructors, filtering, custom painting and event handling that the
// pure-logic suites never reach.
class TstWidgets : public QObject {
  Q_OBJECT
private slots:
  void commandPaletteFiltersAndRuns() {
    int ran = -1;
    QList<CommandPalette::Command> cmds{
        {QStringLiteral("Open settings"), [&]() { ran = 0; }},
        {QStringLiteral("Toggle theme"), [&]() { ran = 1; }},
        {QStringLiteral("Reload page"), [&]() { ran = 2; }},
    };
    CommandPalette palette(cmds);
    auto *edit = palette.findChild<QLineEdit *>();
    auto *list = palette.findChild<QListWidget *>();
    QVERIFY(edit);
    QVERIFY(list);
    QCOMPARE(list->count(), 3); // empty query shows all (refilter ran in ctor)

    edit->setText(QStringLiteral("theme")); // textChanged → refilter
    QVERIFY(list->count() >= 1);
    QVERIFY(list->count() < 3); // unrelated commands filtered out

    // Down/Up move the selection and Return runs it, all via the search box's
    // installed event filter.
    QTest::keyClick(edit, Qt::Key_Down);
    QTest::keyClick(edit, Qt::Key_Up);
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(ran >= 0);
    QCOMPARE(palette.result(), int(QDialog::Accepted));

    // A query that matches nothing empties the list without crashing.
    CommandPalette empty(cmds);
    empty.findChild<QLineEdit *>()->setText(QStringLiteral("zzzzzzzz"));
    QCOMPARE(empty.findChild<QListWidget *>()->count(), 0);
  }

  void accountTabBarQueriesAndClicks() {
    AccountTabBar bar;
    bar.resize(360, 36);
    const int a = bar.addTab(QStringLiteral("Work"));
    bar.setTabData(a, QStringLiteral("work"));
    const int b = bar.addTab(QStringLiteral("Home"));
    bar.setTabData(b, QStringLiteral("home"));
    bar.addTab(QStringLiteral("+")); // the affordance carries no tab data

    QCOMPARE(bar.indexOfAccount(QStringLiteral("work")), a);
    QCOMPARE(bar.indexOfAccount(QStringLiteral("home")), b);
    QCOMPARE(bar.indexOfAccount(QStringLiteral("missing")), -1);

    // The custom strip paints without crashing, and a plain click (no drag)
    // selects through mousePress/mouseRelease.
    QVERIFY(!bar.grab().isNull());
    QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier,
                      bar.tabRect(b).center());
    QCOMPARE(bar.currentIndex(), b);

    // A press then a move that stays within the strip exercises mouseMoveEvent
    // without crossing the detach margin (which would start a blocking QDrag).
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier,
                      bar.tabRect(a).center());
    QMouseEvent mv(QEvent::MouseMove, QPointF(bar.tabRect(b).center()),
                   bar.mapToGlobal(bar.tabRect(b).center()), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &mv);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier,
                        bar.tabRect(b).center());
  }

  void accountTabBarIsADropTarget() {
    const QString mimeType =
        QStringLiteral("application/x-whatly-account-tab");
    AccountTabBar bar;
    bar.resize(360, 36);
    bar.setAcceptDrops(true); // the real host enables drops on the strip
    bar.setTabData(bar.addTab(QStringLiteral("Work")), QStringLiteral("work"));
    bar.setTabData(bar.addTab(QStringLiteral("Home")), QStringLiteral("home"));

    QMimeData data;
    data.setData(mimeType, QByteArray("home"));

    QDragEnterEvent enter(bar.tabRect(0).center(), Qt::MoveAction, &data,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &enter);
    QVERIFY(enter.isAccepted());

    QDragMoveEvent move(bar.tabRect(1).center(), Qt::MoveAction, &data,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &move);

    // With a drag hovering, paintEvent draws the insertion indicator.
    QVERIFY(!bar.grab().isNull());

    QSignalSpy dropped(&bar, &AccountTabBar::accountDropped);
    QDropEvent drop(QPointF(bar.tabRect(1).center()), Qt::MoveAction, &data,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &drop);
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.first().at(0).toString(), QStringLiteral("home"));

    // A foreign payload falls through to the base class, unaccepted.
    QMimeData other;
    other.setText(QStringLiteral("nope"));
    QDragEnterEvent foreign(bar.tabRect(0).center(), Qt::MoveAction, &other,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &foreign);

    QDragLeaveEvent leave;
    QApplication::sendEvent(&bar, &leave); // clears the indicator
  }

  void dictionaryDelegateRenders() {
    const QRect row(0, 0, 200, 24);
    const QRect btn = DictionaryRowDelegate::actionRect(row);
    QVERIFY(!btn.isEmpty());
    QVERIFY(row.contains(btn));
    QVERIFY(!DictionaryRowDelegate::noteRect(row).isEmpty());
    QVERIFY(DictionaryRowDelegate::actionRect(QRect(0, 0, 4, 24)).isEmpty());

    QListView view;
    QStandardItemModel model;
    const auto add = [&](bool installed, DictionaryRows::Action action,
                         int progress, qint64 size) {
      auto *it = new QStandardItem(QStringLiteral("Language"));
      it->setCheckable(true);
      it->setCheckState(installed ? Qt::Checked : Qt::Unchecked);
      it->setData(installed, DictionaryRows::InstalledRole);
      it->setData(int(action), DictionaryRows::ActionRole);
      it->setData(progress, DictionaryRows::ProgressRole);
      it->setData(QVariant::fromValue<qint64>(size), DictionaryRows::SizeRole);
      model.appendRow(it);
    };
    add(true, DictionaryRows::Action::Delete, DictionaryRows::Idle, 0);
    add(false, DictionaryRows::Action::Download, DictionaryRows::Idle, 838860);
    add(false, DictionaryRows::Action::Download, 45, 0); // downloading → spinner
    add(false, DictionaryRows::Action::Download, DictionaryRows::Failed, 0);
    add(true, DictionaryRows::Action::None, DictionaryRows::Idle, 0); // bundled

    view.setModel(&model);
    DictionaryRowDelegate delegate(&view);
    view.setItemDelegate(&delegate);
    view.resize(240, 200);

    QPixmap canvas(240, 30);
    for (int r = 0; r < model.rowCount(); ++r) {
      const QModelIndex idx = model.index(r, 0);
      QStyleOptionViewItem opt;
      opt.rect = QRect(0, 0, 240, 28);
      opt.widget = &view;
      opt.palette = view.palette();
      opt.font = view.font();
      opt.fontMetrics = QFontMetrics(opt.font);
      opt.state = QStyle::State_Enabled;
      if (r == 0)
        opt.state |= QStyle::State_Selected; // selected-ink branch
      canvas.fill(Qt::white);
      QPainter p(&canvas);
      delegate.paint(&p, opt, idx);
      p.end();
      const QSize hint = delegate.sizeHint(opt, idx);
      QVERIFY(hint.width() > 0);
      QVERIFY(hint.height() >= 22);
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// File-level helpers driven on real temporary trees: the tar backup round trip,
// the drop reader's file-to-script pass, drop resolution, and the linger-tip
// event filter.
class TstFileOps : public QObject {
  Q_OBJECT

  static void writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
  }

private slots:
  void backupMakeAndExtract() {
    QTemporaryDir src;
    QVERIFY(src.isValid());
    writeFile(src.path() + QStringLiteral("/one.txt"), "first");
    QVERIFY(QDir(src.path()).mkpath(QStringLiteral("sub")));
    writeFile(src.path() + QStringLiteral("/sub/two.txt"), "second");

    QTemporaryDir out;
    QVERIFY(out.isValid());
    const QString archive = out.path() + QStringLiteral("/backup.tgz");
    QString error;
    // Real tar: makeArchive → extractArchive must reproduce the tree.
    QVERIFY2(Backup::makeArchive(src.path(), archive, &error),
             qPrintable(error));
    QVERIFY(QFileInfo::exists(archive));
    const QString dest = out.path() + QStringLiteral("/restored");
    QVERIFY2(Backup::extractArchive(archive, dest, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(dest + QStringLiteral("/one.txt")));
    QVERIFY(QFileInfo::exists(dest + QStringLiteral("/sub/two.txt")));

    // A source that is not there is refused, not archived empty.
    QVERIFY(!Backup::makeArchive(src.path() + QStringLiteral("/missing"),
                                 archive, &error));
    QVERIFY(!error.isEmpty());
  }

  void backupErrorBranches() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString blocker = root.path() + QStringLiteral("/afile");
    writeFile(blocker, "x"); // a plain file where a directory is needed
    const QString underFile = blocker + QStringLiteral("/sub");
    QString error;

    // extractArchive cannot mkpath a directory under a file.
    QVERIFY(!Backup::extractArchive(root.path() + QStringLiteral("/none.tgz"),
                                    underFile, &error));
    QVERIFY(!error.isEmpty());

    // makeArchive with an archive path under a file makes tar fail.
    QVERIFY(!Backup::makeArchive(root.path(), underFile, &error));

    // copyDirRecursive into a destination that cannot be created fails.
    error.clear();
    QVERIFY(!Backup::copyDirRecursive(root.path(), underFile, &error));
  }

  void profileExportImportRoundTrip() {
    // Runs entirely inside the test-mode locations (QStandardPaths test mode is
    // on for the whole suite), so it never touches a real profile.
    QSettings &s = SettingsManager::instance().settings();
    s.setValue(QStringLiteral("backup/probe"), QStringLiteral("v"));
    s.sync(); // make sure the .conf exists on disk for exportProfile to copy

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(QDir().mkpath(appData + QStringLiteral("/sub")));
    writeFile(appData + QStringLiteral("/probe.txt"), "data");
    writeFile(appData + QStringLiteral("/sub/nested.txt"), "nested");

    QTemporaryDir out;
    QVERIFY(out.isValid());
    const QString archive = out.path() + QStringLiteral("/profile.tgz");
    QString error;
    QVERIFY2(Backup::exportProfile(archive, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(archive));
    // Import extracts and copies the settings + app data back into place.
    QVERIFY2(Backup::importProfile(archive, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(appData + QStringLiteral("/probe.txt")));
    QVERIFY(QFileInfo::exists(appData + QStringLiteral("/sub/nested.txt")));

    s.remove(QStringLiteral("backup/probe"));
    QFile::remove(appData + QStringLiteral("/probe.txt"));
    QDir(appData + QStringLiteral("/sub")).removeRecursively();
  }

  void dropReaderReadsFilesIntoScript() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = dir.path() + QStringLiteral("/hello.txt");
    const QString b = dir.path() + QStringLiteral("/world.txt");
    writeFile(a, "hello"); // base64 → aGVsbG8=
    writeFile(b, "world");
    writeFile(dir.path() + QStringLiteral("/empty.txt"), QByteArray());

    // plan() keeps real non-empty files and drops the empty one and the dir.
    const DropReader::Plan plan = DropReader::plan(
        {a, b, dir.path() + QStringLiteral("/empty.txt"), dir.path()});
    QCOMPARE(plan.accepted.size(), 2);
    QVERIFY(plan.totalBytes > 0);

    DropReader reader(plan.accepted, plan.totalBytes);
    QSignalSpy progress(&reader, &DropReader::progress);
    QSignalSpy finished(&reader, &DropReader::finished);
    reader.run();
    QCOMPARE(finished.count(), 1);
    QVERIFY(progress.count() >= 1);
    QVERIFY(!reader.script().isEmpty());
    QVERIFY(reader.script().contains(QStringLiteral("aGVsbG8="))); // "hello"

    // A reader torn down before it runs drops everything and produces nothing.
    DropReader cancelled(plan.accepted, plan.totalBytes);
    cancelled.cancel();
    cancelled.run();
    QVERIFY(cancelled.script().isEmpty());
  }

  void dropResolveFallsBackWhenNoReadableFile() {
    // A local-file URL to something that is not readable: the readable-probe
    // pass yields nothing, so the last-resort pass hands the path back anyway.
    QMimeData mime;
    const QString ghost = QStringLiteral("/nonexistent/whatly-drop-test.bin");
    mime.setUrls({QUrl::fromLocalFile(ghost)});
    const QStringList paths = DropResolve::droppedFilePaths(&mime);
    QCOMPARE(paths.size(), 1);
    QCOMPARE(paths.first(), ghost);

    // The portal branch is reached when the portal mime is present; without a
    // running portal it stays invalid and falls through without crashing.
    QMimeData portal;
    portal.setData(QStringLiteral("application/vnd.portal.filetransfer"),
                   QByteArray("some-key"));
    DropResolve::droppedFilePaths(&portal); // no assertion: exercises the path

    QVERIFY(DropResolve::droppedFilePaths(nullptr).isEmpty());
  }

  void debugLogAppendAndTrim() {
    // (The level-stamping message handler cannot be exercised here: QtTest
    // installs its own handler for the duration of each test, so qWarning never
    // reaches DebugLog's. append/recent are the public surface.)
    DebugLog::append(QStringLiteral("cov-marker-unique"));
    QVERIFY(DebugLog::recent(400).contains(QStringLiteral("cov-marker-unique")));

    // Past the ring-buffer cap the oldest lines are dropped, not the newest.
    for (int i = 0; i < 600; ++i)
      DebugLog::append(QStringLiteral("filler-%1").arg(i));
    const QString tail = DebugLog::recent(500);
    QVERIFY(tail.contains(QStringLiteral("filler-599")));
    QVERIFY(!tail.contains(QStringLiteral("cov-marker-unique"))); // aged out
  }

  void debugLogRotatesCaptureFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/webengine.log");
    writeFile(path, "previous session");
    QVERIFY(DebugLog::rotateCaptureFile(path));
    QVERIFY(QFileInfo::exists(path));                            // fresh, empty
    QCOMPARE(QFileInfo(path).size(), 0LL);
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".prev"))); // old kept aside
  }

  void lingerTipEventFilter() {
    QWidget w;
    w.resize(120, 40);
    int eligibleCalls = 0;
    LingerTip::install(&w, QStringLiteral("a tip"),
                       [&](const QPoint &) {
                         ++eligibleCalls;
                         return true;
                       });
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&w, &enter);
    QMouseEvent move(QEvent::MouseMove, QPointF(6, 6), QPointF(6, 6),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &move);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&w, &leave);
    QVERIFY(eligibleCalls >= 2); // the filter consulted eligibility on enter+move

    // An ineligible position stops the timer rather than starting it.
    QWidget w2;
    w2.resize(120, 40);
    LingerTip::install(&w2, QStringLiteral("x"),
                       [](const QPoint &) { return false; });
    QMouseEvent m2(QEvent::MouseMove, QPointF(2, 2), QPointF(2, 2),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&w2, &m2);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ChatExport's pure formatting: every MIME → extension branch, the file-name
// sanitiser's edges, and transcriptLine across text / attached / omitted media
// and the "You" / named / anonymous sender cases.
class TstChatExportMore : public QObject {
  Q_OBJECT
private slots:
  void extForMimeCoversEveryType() {
    using ChatExport::extForMime;
    QCOMPARE(extForMime(QStringLiteral("image/jpeg")), QStringLiteral(".jpg"));
    QCOMPARE(extForMime(QStringLiteral("image/jpg")), QStringLiteral(".jpg"));
    QCOMPARE(extForMime(QStringLiteral("image/png")), QStringLiteral(".png"));
    QCOMPARE(extForMime(QStringLiteral("image/webp")), QStringLiteral(".webp"));
    QCOMPARE(extForMime(QStringLiteral("image/gif")), QStringLiteral(".gif"));
    QCOMPARE(extForMime(QStringLiteral("video/mp4")), QStringLiteral(".mp4"));
    QCOMPARE(extForMime(QStringLiteral("video/webm")), QStringLiteral(".webm"));
    QCOMPARE(extForMime(QStringLiteral("audio/ogg")), QStringLiteral(".ogg"));
    QCOMPARE(extForMime(QStringLiteral("audio/ogg; codecs=opus")),
             QStringLiteral(".ogg"));
    QCOMPARE(extForMime(QStringLiteral("audio/mpeg")), QStringLiteral(".mp3"));
    QCOMPARE(extForMime(QStringLiteral("audio/mp4")), QStringLiteral(".m4a"));
    QCOMPARE(extForMime(QStringLiteral("audio/aac")), QStringLiteral(".m4a"));
    QCOMPARE(extForMime(QStringLiteral("application/pdf")),
             QStringLiteral(".pdf"));
    // Fallback: a short, clean subtype becomes the extension; anything else
    // (too long, no slash) falls back to .bin.
    QCOMPARE(extForMime(QStringLiteral("application/xyz")),
             QStringLiteral(".xyz"));
    QCOMPARE(extForMime(QStringLiteral("application/octet-stream")),
             QStringLiteral(".bin"));
    QCOMPARE(extForMime(QStringLiteral("nonsense")), QStringLiteral(".bin"));
  }

  void sanitizeFileNameEdges() {
    using ChatExport::sanitizeFileName;
    QCOMPARE(sanitizeFileName(QString()), QStringLiteral("chat")); // empty
    QCOMPARE(sanitizeFileName(QStringLiteral("....")),
             QStringLiteral("chat")); // trailing dots stripped to nothing
    QCOMPARE(sanitizeFileName(QStringLiteral("a/b:c*?")),
             QStringLiteral("a_b_c__")); // illegal chars replaced
    QCOMPARE(sanitizeFileName(QString(200, QLatin1Char('a'))).size(), 120);
  }

  void transcriptLineVariants() {
    using ChatExport::Message;
    QList<Message> msgs;
    msgs.append({QStringLiteral("09:00"), QStringLiteral("Alice"),
                 QStringLiteral("in"), QStringLiteral("text"),
                 QStringLiteral("hi"), QString(), false});
    msgs.append({QStringLiteral("09:01"), QString(), QStringLiteral("out"),
                 QStringLiteral("text"), QStringLiteral("reply"), QString(),
                 false});
    msgs.append({QStringLiteral("09:02"), QString(), QStringLiteral("in"),
                 QStringLiteral("image"), QStringLiteral("cap"),
                 QStringLiteral("img.jpg"), false});
    msgs.append({QStringLiteral("09:03"), QString(), QStringLiteral("out"),
                 QStringLiteral("video"), QString(), QString(), true});
    const QString t = ChatExport::buildTranscript(QStringLiteral("My Chat"),
                                                  msgs);
    QVERIFY(t.contains(QStringLiteral("Alice: hi")));
    QVERIFY(t.contains(QStringLiteral("You: reply")));   // anonymous outgoing
    QVERIFY(t.contains(QStringLiteral("<attached: media/img.jpg> cap")));
    QVERIFY(t.contains(QStringLiteral("<video omitted>")));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// DictionaryManager's pure and file-level surface (URLs, display names, and the
// remove/isRemovable file checks), plus Dictionaries::syncDictionaryDirs mirroring
// a bundle and dropping stale links. The network catalogue/download is not here.
class TstDictionaryManagerMore : public QObject {
  Q_OBJECT
private slots:
  void urlsAndDisplayName() {
    qunsetenv("WHATLY_DICT_BASE_URL");
    const QString base = DictionaryManager::catalogBaseUrl();
    QVERIFY(!base.isEmpty());
    QCOMPARE(DictionaryManager::manifestUrl(),
             base + QStringLiteral("/manifest.json"));
    QCOMPARE(DictionaryManager::assetUrl(QStringLiteral("es")),
             base + QStringLiteral("/es.bdic"));

    // The env var overrides the built-in base.
    qputenv("WHATLY_DICT_BASE_URL", "https://example.test/dic");
    QCOMPARE(DictionaryManager::catalogBaseUrl(),
             QStringLiteral("https://example.test/dic"));
    qunsetenv("WHATLY_DICT_BASE_URL");

    QVERIFY(!DictionaryManager::displayName(QStringLiteral("es")).isEmpty());
    // An unrecognised code is shown raw rather than as the C locale.
    QCOMPARE(DictionaryManager::displayName(QStringLiteral("zz_NOPE")),
             QStringLiteral("zz_NOPE"));
  }

  void removeAndIsRemovable() {
    const QString dir = Dictionaries::userDictionaryPath();
    QVERIFY(!dir.isEmpty());
    QVERIFY(QDir().mkpath(dir));
    const QString code = QStringLiteral("xx_COV");
    const QString path = QDir(dir).filePath(code + QStringLiteral(".bdic"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("bdic");
    }
    DictionaryManager dm;
    QVERIFY(DictionaryManager::isRemovable(code)); // real file, not a symlink
    QVERIFY(DictionaryManager::installed().contains(code));
    QVERIFY(dm.remove(code));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!DictionaryManager::isRemovable(code)); // gone now
    QVERIFY(!dm.remove(code));                      // nothing left to remove
  }

  // First-run fetch selection (issue #110): the language chosen from the
  // manifest for a system with nothing installed. Pure, so no network is needed.
  void firstRunFetchCandidate() {
    const QStringList manifest = {
        QStringLiteral("en_US"), QStringLiteral("es_ES"),
        QStringLiteral("es_MX"), QStringLiteral("de_DE")};
    // Full name wins when the manifest carries it.
    QCOMPARE(
        Dictionaries::systemFetchCandidate(manifest, QStringLiteral("es_ES")),
        QStringLiteral("es_ES"));
    // A territory the manifest lacks falls back to another form of the language,
    // so an es_AR system still gets Spanish rather than nothing.
    const QString ar =
        Dictionaries::systemFetchCandidate(manifest, QStringLiteral("es_AR"));
    QVERIFY(ar == QStringLiteral("es_ES") || ar == QStringLiteral("es_MX"));
    // The plain-language code is preferred over a territory form when present.
    const QStringList withPlain = {QStringLiteral("es"),
                                   QStringLiteral("es_ES")};
    QCOMPARE(
        Dictionaries::systemFetchCandidate(withPlain, QStringLiteral("es_AR")),
        QStringLiteral("es"));
    // A language the manifest does not carry gives nothing (the eo case).
    QVERIFY(Dictionaries::systemFetchCandidate(manifest, QStringLiteral("eo"))
                .isEmpty());
    // Empty inputs are safe.
    QVERIFY(Dictionaries::systemFetchCandidate({}, QStringLiteral("es_ES"))
                .isEmpty());
    QVERIFY(
        Dictionaries::systemFetchCandidate(manifest, QString()).isEmpty());
  }

  void manifestTagIsStableAndOrderIndependent() {
    const QStringList a = {QStringLiteral("en_US"), QStringLiteral("es_ES")};
    const QStringList b = {QStringLiteral("es_ES"), QStringLiteral("en_US")};
    QCOMPARE(Dictionaries::manifestTag(a), Dictionaries::manifestTag(b));
    const QStringList c = {QStringLiteral("en_US"), QStringLiteral("es_ES"),
                           QStringLiteral("de_DE")};
    QVERIFY(Dictionaries::manifestTag(a) != Dictionaries::manifestTag(c));
  }

  // A Settings-driven removal records the opt-out, so the first-run fetch does
  // not pull a dictionary back in behind the user's back (issue #110).
  void removeRecordsOptOut() {
    auto &s = SettingsManager::instance().settings();
    const QString key = QStringLiteral("dictionaries/systemFetchOptOut");
    const QVariant saved = s.value(key);
    s.remove(key);

    const QString dir = Dictionaries::userDictionaryPath();
    QVERIFY(QDir().mkpath(dir));
    const QString code = QStringLiteral("yy_OPT");
    const QString path = QDir(dir).filePath(code + QStringLiteral(".bdic"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("bdic");
    }
    DictionaryManager dm;
    QVERIFY(dm.remove(code));
    QVERIFY(s.value(key).toBool());

    // Restore, so this global setting does not leak into other tests.
    if (saved.isValid())
      s.setValue(key, saved);
    else
      s.remove(key);
  }

  void preferredDictionaryFallsBackToLocale() {
    auto &s = SettingsManager::instance().settings();
    const QVariant saved = s.value(QStringLiteral("spellCheckLanguage"));
    const QString dir = Dictionaries::userDictionaryPath();
    QVERIFY(QDir().mkpath(dir));
    const QString path = QDir(dir).filePath(QStringLiteral("es_ES.bdic"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
    }
    // A stored choice that is not installed falls through to the locale-name,
    // then language, then first-available logic.
    s.setValue(QStringLiteral("spellCheckLanguage"),
               QStringLiteral("zz_NONE"));
    QVERIFY(!Dictionaries::preferredDictionary().isEmpty());
    s.setValue(QStringLiteral("spellCheckLanguage"), saved);
    QFile::remove(path);
  }

  void syncMirrorsBundleAndDropsStaleLinks() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString bundled = root.path() + QStringLiteral("/bundled");
    const QString user = root.path() + QStringLiteral("/user");
    QVERIFY(QDir().mkpath(bundled));
    QVERIFY(QDir().mkpath(user));
    {
      QFile f(bundled + QStringLiteral("/de_DE.bdic"));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
    }
    // A stale symlink pointing at a file that is not there must be dropped.
    QFile::link(root.path() + QStringLiteral("/gone.bdic"),
                user + QStringLiteral("/old_XX.bdic"));

    Dictionaries::syncDictionaryDirs(user, bundled);

    QVERIFY(QFileInfo::exists(user + QStringLiteral("/de_DE.bdic"))); // mirrored
    QVERIFY(!QFileInfo(user + QStringLiteral("/old_XX.bdic"))
                 .exists()); // stale link removed
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ScheduledMessages driven through its due-checking machinery: a reminder that
// fires at once, a message that asks to be sent, recurrence advancing on
// success, and failure being recorded — plus the pure recurrence helpers.
class TstScheduledDue : public QObject {
  Q_OBJECT
  using Recurrence = ScheduledMessages::Recurrence;
  using Status = ScheduledMessages::Status;

private slots:
  void recurrenceHelpers() {
    const QDateTime monday(QDate(2026, 1, 5), QTime(9, 0)); // a Monday
    QVERIFY(!ScheduledMessages::nextOccurrence(monday, Recurrence::None)
                 .isValid());
    QCOMPARE(ScheduledMessages::nextOccurrence(monday, Recurrence::Daily).date(),
             monday.date().addDays(1));
    QCOMPARE(ScheduledMessages::nextOccurrence(monday, Recurrence::Weekly).date(),
             monday.date().addDays(7));
    // Weekdays from a Friday skips the weekend to the following Monday.
    const QDateTime friday(QDate(2026, 1, 9), QTime(9, 0));
    QCOMPARE(ScheduledMessages::nextOccurrence(friday, Recurrence::Weekdays)
                 .date()
                 .dayOfWeek(),
             1);
    for (Recurrence r : {Recurrence::None, Recurrence::Daily,
                         Recurrence::Weekdays, Recurrence::Weekly})
      QVERIFY(!ScheduledMessages::recurrenceLabel(r).isEmpty());
    QVERIFY(!ScheduledMessages::statusLabel(Status::Pending).isEmpty());
    QVERIFY(!ScheduledMessages::statusLabel(Status::Sent).isEmpty());
    QVERIFY(!ScheduledMessages::statusLabel(Status::Failed).isEmpty());
  }

  void firesDueRemindersAndSends() {
    ScheduledMessages sm;
    // Start from a known-empty state so a stray entry from another test cannot
    // occupy the single send slot.
    for (const auto &e : sm.entries())
      sm.remove(e.id);
    sm.start();

    QSignalSpy reminders(&sm, &ScheduledMessages::reminderDue);
    QSignalSpy sends(&sm, &ScheduledMessages::sendRequested);
    const QDateTime past = QDateTime::currentDateTime().addSecs(-60);

    // A due reminder fires immediately without occupying the send slot.
    const QString rid = sm.add(QStringLiteral("34600000000"),
                               QStringLiteral("Me"), QStringLiteral("ping"),
                               past, Recurrence::None, /*reminder=*/true);
    QVERIFY(reminders.count() >= 1);

    // A due recurring message asks to be sent; success reschedules it.
    const QString mid = sm.add(QStringLiteral("34600000000"), QString(),
                               QStringLiteral("hola"), past, Recurrence::Daily,
                               /*reminder=*/false);
    QVERIFY(sends.count() >= 1);
    sm.reportResult(mid, true, QString());
    if (const auto list = sm.entries(); true)
      for (const auto &e : list)
        if (e.id == mid)
          QCOMPARE(e.status, Status::Pending); // advanced, not completed

    // A failure is recorded on the entry.
    const QString fid =
        sm.add(QStringLiteral("34600000000"), QString(),
               QStringLiteral("fallo"), past, Recurrence::None, false);
    sm.reportResult(fid, false, QStringLiteral("nope"));
    for (const auto &e : sm.entries())
      if (e.id == fid) {
        QCOMPARE(e.status, Status::Failed);
        QCOMPARE(e.error, QStringLiteral("nope"));
      }

    sm.removeCompleted(); // drops the Failed one
    sm.remove(rid);
    sm.remove(mid);
    for (const auto &e : sm.entries()) // leave the shared store as we found it
      sm.remove(e.id);
  }

  void persistsAcrossInstances() {
    {
      ScheduledMessages sm;
      for (const auto &e : sm.entries())
        sm.remove(e.id);
      sm.add(QStringLiteral("34600000000"), QStringLiteral("N"),
             QStringLiteral("later"),
             QDateTime::currentDateTime().addDays(1), Recurrence::Weekly,
             false); // saved to disk, due tomorrow so it never fires here
    }
    ScheduledMessages fresh; // its constructor load()s the saved entry back
    QVERIFY(!fresh.entries().isEmpty());
    // find() returning nullptr: reporting a result for an unknown id is a no-op.
    fresh.reportResult(QStringLiteral("no-such-id"), true, QString());
    for (const auto &e : fresh.entries()) // leave the shared store clean
      fresh.remove(e.id);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// A few Utils helpers whose branches the existing tests miss: the cache-size unit
// picker, genRand's "no character classes" recursion, the XML entity round trip,
// and getInstallType reading the packaging environment.
class TstUtilsCoverage : public QObject {
  Q_OBJECT
  static void writeSized(const QString &path, int bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray(bytes, 'x'));
    f.close();
  }
private slots:
  void refreshCacheSizeUnits() {
    QTemporaryDir mb;
    writeSized(mb.path() + QStringLiteral("/big.bin"), 2 * 1024 * 1024);
    QVERIFY(Utils::refreshCacheSize(mb.path()).contains(QStringLiteral("MB")));
    QTemporaryDir kb;
    writeSized(kb.path() + QStringLiteral("/mid.bin"), 4 * 1024);
    QVERIFY(Utils::refreshCacheSize(kb.path()).contains(QStringLiteral("kB")));
    QTemporaryDir b;
    writeSized(b.path() + QStringLiteral("/tiny.bin"), 8);
    QVERIFY(Utils::refreshCacheSize(b.path()).endsWith(QStringLiteral(" B")));
  }

  void genRandRecursesWithoutClasses() {
    // No character class enabled: the function recurses with all of them on.
    QCOMPARE(Utils::genRand(8, false, false, false).size(), 8);
  }

  void xmlEntitiesRoundTrip() {
    const QString raw = QStringLiteral("a&b<c>\"d'e");
    const QString enc = Utils::encodeXML(raw);
    QVERIFY(enc.contains(QStringLiteral("&amp;")));
    QVERIFY(enc.contains(QStringLiteral("&lt;")));
    QCOMPARE(Utils::decodeXML(enc), raw);
  }

  void watermarkStyling() {
    Utils::makeWatermark(nullptr); // a null label is a no-op
    {
      QLabel l; // a point-sized font is scaled down
      QFont f = l.font();
      f.setPointSizeF(12.0);
      l.setFont(f);
      Utils::makeWatermark(&l);
      QVERIFY(l.graphicsEffect() != nullptr);
    }
    {
      QLabel l; // a pixel-sized font takes the other branch
      QFont f = l.font();
      f.setPixelSize(20);
      l.setFont(f);
      Utils::makeWatermark(&l);
      QVERIFY(l.graphicsEffect() != nullptr);
    }
  }

  void installTypeReadsEnvironment() {
    qputenv("INSTALL_TYPE", "custom");
    QCOMPARE(Utils::getInstallType(), QStringLiteral("custom"));
    qunsetenv("INSTALL_TYPE");
    qputenv("SNAP", "/snap/whatly/current");
    QCOMPARE(Utils::getInstallType(), QStringLiteral("snap"));
    qunsetenv("SNAP");
    qputenv("FLATPAK_ID", "net.shakaran.whatly");
    QCOMPARE(Utils::getInstallType(), QStringLiteral("flatpak"));
    qunsetenv("FLATPAK_ID");
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// A grab bag of otherwise-unreached branches: the AI response parser's no-text
// path, the optional API-key setter, and the language-label variant/bare-code
// shapes.
class TstMiscCoverage : public QObject {
  Q_OBJECT
private slots:
  void aiParserAndApiKey() {
    Ai::setApiKey(QStringLiteral("k")); // the optional-key setter
    QString err;
    // A well-formed envelope carrying no message content is a failure with a
    // human-readable reason, not an empty success.
    const QString out = Ai::parseChatResponse(
        QByteArray(R"({"choices":[{"message":{}}]})"), &err);
    QVERIFY(out.isEmpty());
    QVERIFY(!err.isEmpty());
    Ai::setApiKey(QString());
  }

  void languageLabelVariantAndBareCode() {
    // A variant tag Qt folds into a plain locale is spelled out by territory
    // and variant rather than dumped as a raw code.
    const QString neu = Dictionaries::languageLabel(QStringLiteral("de_DE_neu"));
    QVERIFY(neu.contains(QStringLiteral("neu")));
    // A bare language Qt would over-qualify keeps the code as its disambiguator.
    QVERIFY(!Dictionaries::languageLabel(QStringLiteral("eo")).isEmpty());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// CustomJs add/enable/remove on a real temporary addon store, and the settings
// search text extraction over a small built layout.
class TstMoreCoverage : public QObject {
  Q_OBJECT
private slots:
  void customJsAddonLifecycle() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString js = dir.path() + QStringLiteral("/my addon.js");
    {
      QFile f(js);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("console.log('hi')");
    }
    QString error;
    const QString name = CustomJs::addFromFile(js, &error);
    QVERIFY2(!name.isEmpty(), qPrintable(error));
    QVERIFY(!CustomJs::sourceOf(name).isEmpty());
    CustomJs::setEnabled(name, true);
    QVERIFY(CustomJs::isEnabled(name));
    CustomJs::setEnabled(name, false);
    QVERIFY(!CustomJs::isEnabled(name));
    // A second import of the same base name is suffixed rather than clobbering.
    const QString name2 = CustomJs::addFromFile(js, &error);
    QVERIFY(!name2.isEmpty());
    QVERIFY(name2 != name);
    CustomJs::remove(name);
    CustomJs::remove(name2);
  }

  void autoReplyJsonErrors() {
    QString err;
    QVERIFY(AutoReply::rulesFromJson(QByteArray("not json"), &err).isEmpty());
    QVERIFY(!err.isEmpty()); // parse error
    err.clear();
    QVERIFY(AutoReply::rulesFromJson(QByteArray("{}"), &err).isEmpty());
    QVERIFY(!err.isEmpty()); // an object is not the expected array
  }

  void dictionaryManagerDownloadGuards() {
    DictionaryManager dm;
    QSignalSpy done(&dm, &DictionaryManager::downloadFinished);
    DictionaryEntry empty; // no code → download is a no-op
    dm.download(empty);
    QCOMPARE(done.count(), 0);
  }

  void sessionBackupDataRootFollowsSettings() {
    SessionBackup::setPathsForTesting(QString(), QString()); // no override
    auto &s = SettingsManager::instance().settings();
    const QVariant saved = s.value(QStringLiteral("storage/dataDir"));
    const QVariant savedEnabled = s.value(QStringLiteral("sessionBackup/enabled"));
    s.setValue(QStringLiteral("sessionBackup/enabled"), true);

    // The storage/dataDir override branch of dataRoot().
    QTemporaryDir d;
    QVERIFY(d.isValid());
    s.setValue(QStringLiteral("storage/dataDir"), d.path());
    SessionBackup::runStartupRecovery();
    // The default AppLocalDataLocation branch.
    s.remove(QStringLiteral("storage/dataDir"));
    SessionBackup::runStartupRecovery();

    if (saved.isValid())
      s.setValue(QStringLiteral("storage/dataDir"), saved);
    s.setValue(QStringLiteral("sessionBackup/enabled"), savedEnabled);
  }

  void settingsSearchTextExtraction() {
    QVERIFY(SettingsSearch::matches(
        SettingsSearch::normalise(QStringLiteral("Básico spell-check")),
        QStringLiteral("basico")));
    QVERIFY(!SettingsSearch::matches(QStringLiteral("nothing here"),
                                     QStringLiteral("zzz")));

    // A widget tree read through the layout: the label's text is found, and so is
    // a nested child's.
    QWidget root;
    auto *layout = new QVBoxLayout(&root);
    auto *label = new QLabel(QStringLiteral("Memory limit"), &root);
    layout->addWidget(label);
    auto *group = new QWidget(&root);
    auto *inner = new QVBoxLayout(group);
    inner->addWidget(new QLabel(QStringLiteral("nested option"), group));
    layout->addWidget(group);

    QVERIFY(SettingsSearch::textOf(label).contains(QStringLiteral("Memory")));
    const QString viaItem =
        SettingsSearch::textOfItem(layout->itemAt(0));
    QVERIFY(viaItem.contains(QStringLiteral("Memory")));
    const QString groupText =
        SettingsSearch::textOfItem(layout->itemAt(1));
    QVERIFY(groupText.contains(QStringLiteral("nested")));
  }
};

// A one-shot localhost HTTP server that answers the next request with 200 + a
// fixed JSON body. Lets the network clients run their real request/reply path
// against a stand-in instead of a live service.
class MockHttpServer : public QObject {
  Q_OBJECT
public:
  QString start(const QByteArray &body) {
    m_body = body;
    if (!m_server.listen(QHostAddress::LocalHost, 0))
      return QString();
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
      QTcpSocket *sock = m_server.nextPendingConnection();
      connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
        sock->readAll(); // the request content does not matter to the mock
        QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                          "Content-Length: " +
                          QByteArray::number(m_body.size()) +
                          "\r\nConnection: close\r\n\r\n" + m_body;
        sock->write(resp);
        sock->flush();
        sock->disconnectFromHost();
      });
    });
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
  }

private:
  QTcpServer m_server;
  QByteArray m_body;
};

// The request/reply paths of the network clients, driven against MockHttpServer.
class TstNetworkClients : public QObject {
  Q_OBJECT
private slots:
  void ollamaCheckParsesTags() {
    MockHttpServer mock;
    const QString url = mock.start(
        R"({"models":[{"name":"llama3:latest"},{"name":"phi3:mini"}]})");
    QVERIFY(!url.isEmpty());
    OllamaManager m;
    QSignalSpy spy(&m, &OllamaManager::checked);
    m.check(url);
    QVERIFY(spy.wait(5000));
    const auto args = spy.takeFirst();
    QVERIFY(args.at(0).toBool()); // reachable
    QVERIFY(args.at(1).toStringList().contains(QStringLiteral("llama3:latest")));
  }

  void ollamaCheckReportsUnreachable() {
    OllamaManager m;
    QSignalSpy spy(&m, &OllamaManager::checked);
    m.check(QStringLiteral("http://127.0.0.1:1")); // nothing is listening there
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.first().at(0).toBool(), false); // the error branch
  }

  void ollamaPullStreamsProgress() {
    MockHttpServer mock;
    // Ollama streams newline-delimited JSON progress, then a final success line.
    const QString url = mock.start(
        "{\"status\":\"pulling\",\"completed\":50,\"total\":100}\n"
        "{\"status\":\"success\"}\n");
    QVERIFY(!url.isEmpty());
    OllamaManager m;
    QSignalSpy progress(&m, &OllamaManager::pullProgress);
    QSignalSpy finished(&m, &OllamaManager::pullFinished);
    m.pull(url, QStringLiteral("llama3"));
    QVERIFY(finished.wait(5000));
    QVERIFY(progress.count() >= 1);
    QCOMPARE(finished.first().at(0).toBool(), true);
  }

  void translatorTranslates() {
    MockHttpServer mock;
    const QString url = mock.start(R"({"translatedText":"hola"})");
    QVERIFY(!url.isEmpty());
    Translate::setEndpoint(url);
    Translator t;
    QSignalSpy ok(&t, &Translator::translated);
    QSignalSpy bad(&t, &Translator::failed);
    t.translate(QStringLiteral("hi"), QStringLiteral("es"));
    QVERIFY(ok.wait(5000) || bad.count() > 0);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.takeFirst().at(0).toString(), QStringLiteral("hola"));
    Translate::setEndpoint(QString());
  }

  void aiCompletes() {
    MockHttpServer mock;
    const QByteArray body = R"({"choices":[{"message":{"content":"ok"}}]})";
    const QString url = mock.start(body);
    QVERIFY(!url.isEmpty());
    Ai::setEndpoint(url);
    Ai::setModel(QStringLiteral("m"));
    AiClient c;
    QSignalSpy done(&c, &AiClient::completed);
    QSignalSpy bad(&c, &AiClient::failed);
    c.complete(QStringLiteral("sys"), QStringLiteral("user"));
    QVERIFY(done.wait(5000) || bad.count() > 0);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(done.takeFirst().at(0).toString(), QStringLiteral("ok"));
    Ai::setEndpoint(QString());
  }

  void aiGuardsAndErrors() {
    // No endpoint configured.
    Ai::setEndpoint(QString());
    Ai::setModel(QStringLiteral("m"));
    {
      AiClient c;
      QSignalSpy bad(&c, &AiClient::failed);
      c.complete(QStringLiteral("sys"), QStringLiteral("user"));
      QCOMPARE(bad.count(), 1);
    }
    // Endpoint but no model.
    Ai::setEndpoint(QStringLiteral("http://127.0.0.1:1"));
    Ai::setModel(QString());
    {
      AiClient c;
      QSignalSpy bad(&c, &AiClient::failed);
      c.complete(QStringLiteral("sys"), QStringLiteral("user"));
      QCOMPARE(bad.count(), 1);
    }
    // Endpoint and model, but nothing to send.
    Ai::setModel(QStringLiteral("m"));
    {
      AiClient c;
      QSignalSpy bad(&c, &AiClient::failed);
      c.complete(QStringLiteral("sys"), QStringLiteral("   "));
      QCOMPARE(bad.count(), 1);
    }
    // A reply that is not the expected JSON fails after the round trip.
    {
      MockHttpServer mock;
      Ai::setEndpoint(mock.start("not json at all"));
      AiClient c;
      QSignalSpy bad(&c, &AiClient::failed);
      c.complete(QStringLiteral("sys"), QStringLiteral("hola"));
      QVERIFY(bad.wait(5000));
    }
    // A well-formed request to a port with nothing listening fails on the
    // network error branch of the reply handler.
    {
      Ai::setEndpoint(QStringLiteral("http://127.0.0.1:1"));
      Ai::setModel(QStringLiteral("m"));
      AiClient c;
      QSignalSpy bad(&c, &AiClient::failed);
      c.complete(QStringLiteral("sys"), QStringLiteral("hola"));
      QVERIFY(bad.wait(5000));
    }
    Ai::setEndpoint(QString());
  }

  void translatorGuardsAndErrors() {
    Translate::setEndpoint(QString());
    {
      Translator t;
      QSignalSpy bad(&t, &Translator::failed);
      t.translate(QStringLiteral("hi"), QStringLiteral("es"));
      QCOMPARE(bad.count(), 1); // no endpoint
    }
    Translate::setEndpoint(QStringLiteral("http://127.0.0.1:1"));
    {
      Translator t;
      QSignalSpy bad(&t, &Translator::failed);
      t.translate(QStringLiteral("   "), QStringLiteral("es"));
      QCOMPARE(bad.count(), 1); // nothing to translate
    }
    {
      MockHttpServer mock;
      Translate::setEndpoint(mock.start("not json"));
      Translator t;
      QSignalSpy bad(&t, &Translator::failed);
      t.translate(QStringLiteral("hi"), QStringLiteral("es"));
      QVERIFY(bad.wait(5000));
    }
    {
      // Nothing listening: the reply handler's network-error branch fires.
      Translate::setEndpoint(QStringLiteral("http://127.0.0.1:1"));
      Translator t;
      QSignalSpy bad(&t, &Translator::failed);
      t.translate(QStringLiteral("hi"), QStringLiteral("es"));
      QVERIFY(bad.wait(5000));
    }
    Translate::setEndpoint(QString());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// DictionaryManager's networked catalogue fetch and asset download, pointed at
// the mock server via WHATLY_DICT_BASE_URL: manifest parse, the failure path,
// and a download that is verified and saved / rejected on a bad hash.
class TstDictionaryManagerNet : public QObject {
  Q_OBJECT
  static QString sha256Hex(const QByteArray &d) {
    return QString::fromLatin1(
        QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex());
  }
private slots:
  void cleanup() { qunsetenv("WHATLY_DICT_BASE_URL"); }

  void fetchCatalogParsesManifest() {
    MockHttpServer mock;
    const QString url = mock.start(
        R"({"dictionaries":[{"code":"de_DE","size":4,"sha256":"aa"}]})");
    QVERIFY(!url.isEmpty());
    qputenv("WHATLY_DICT_BASE_URL", url.toUtf8());
    DictionaryManager dm;
    QSignalSpy ready(&dm, &DictionaryManager::catalogReady);
    dm.fetchCatalog();
    QVERIFY(ready.wait(5000));
  }

  void fetchCatalogReportsFailure() {
    qputenv("WHATLY_DICT_BASE_URL", "http://127.0.0.1:1"); // nothing listening
    DictionaryManager dm;
    QSignalSpy failed(&dm, &DictionaryManager::catalogFailed);
    dm.fetchCatalog();
    QVERIFY(failed.wait(5000));
  }

  void downloadVerifiesAndSaves() {
    const QByteArray asset("BDIC-CONTENT");
    MockHttpServer mock;
    const QString url = mock.start(asset);
    qputenv("WHATLY_DICT_BASE_URL", url.toUtf8());
    DictionaryEntry entry;
    entry.code = QStringLiteral("xx_DL");
    entry.size = asset.size();
    entry.sha256 = sha256Hex(asset);
    DictionaryManager dm;
    QSignalSpy done(&dm, &DictionaryManager::downloadFinished);
    dm.download(entry);
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(1).toBool(), true); // verified and written
    QFile::remove(QDir(Dictionaries::userDictionaryPath())
                      .filePath(QStringLiteral("xx_DL.bdic")));
  }

  void downloadRejectsBadHash() {
    const QByteArray asset("BAD");
    MockHttpServer mock;
    const QString url = mock.start(asset);
    qputenv("WHATLY_DICT_BASE_URL", url.toUtf8());
    DictionaryEntry entry;
    entry.code = QStringLiteral("xx_BAD");
    entry.size = asset.size();
    entry.sha256 = QStringLiteral("deadbeef"); // does not match the payload
    DictionaryManager dm;
    QSignalSpy done(&dm, &DictionaryManager::downloadFinished);
    dm.download(entry);
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(1).toBool(), false); // verification failed
  }
};

// A localhost HTTP server that tells the manifest request apart from the asset
// request by path, so the two-step first-run fetch (manifest.json, then the
// <code>.bdic) can be driven end to end against a stand-in.
class MockDictServer : public QObject {
  Q_OBJECT
public:
  QString start(const QByteArray &manifest, const QByteArray &asset) {
    m_manifest = manifest;
    m_asset = asset;
    if (!m_server.listen(QHostAddress::LocalHost, 0))
      return QString();
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
      QTcpSocket *sock = m_server.nextPendingConnection();
      connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
        const QByteArray req = sock->readAll(); // the request line carries the path
        const QByteArray &body =
            req.contains("manifest.json") ? m_manifest : m_asset;
        const QByteArray resp =
            "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
            "Content-Length: " +
            QByteArray::number(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body;
        sock->write(resp);
        sock->flush();
        sock->disconnectFromHost();
      });
    });
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
  }

private:
  QTcpServer m_server;
  QByteArray m_manifest;
  QByteArray m_asset;
};

// The first-run dictionary fetch (issue #110), driven end to end against
// MockDictServer via WHATLY_DICT_BASE_URL. beginFetch() bypasses the
// installed()/opt-out gate, so the pipeline is exercised whatever the host has.
class TstDictionaryBootstrapNet : public QObject {
  Q_OBJECT
  static QString sha256Hex(const QByteArray &d) {
    return QString::fromLatin1(
        QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex());
  }
  static void clearState() {
    auto &s = SettingsManager::instance().settings();
    s.remove(QStringLiteral("dictionaries/systemFetchNotInManifest"));
    s.remove(QStringLiteral("dictionaries/systemFetchManifestTag"));
  }

private slots:
  void cleanup() {
    qunsetenv("WHATLY_DICT_BASE_URL");
    qunsetenv("WHATLY_DICT_TEST_FAST");
    clearState();
  }

  void fetchesTheSystemLanguage() {
    const QString locale = QLocale::system().name();
    QVERIFY(!locale.isEmpty());
    const QByteArray asset("BDicSYSTEMFETCH");
    // The manifest carries the system locale's own code, so the candidate is
    // found whatever the host's locale happens to be.
    const QByteArray manifest =
        QStringLiteral(
            "{\"dictionaries\":[{\"code\":\"%1\",\"size\":%2,\"sha256\":\"%3\"}]}")
            .arg(locale)
            .arg(asset.size())
            .arg(sha256Hex(asset))
            .toUtf8();
    MockDictServer mock;
    const QString url = mock.start(manifest, asset);
    QVERIFY(!url.isEmpty());
    qputenv("WHATLY_DICT_BASE_URL", url.toUtf8());

    DictionaryBootstrap boot;
    QSignalSpy done(&boot, &DictionaryBootstrap::finished);
    boot.beginFetch();
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toBool(), true);       // fetched
    QCOMPARE(done.first().at(1).toString(), locale);   // the system language
    const QString path = QDir(Dictionaries::userDictionaryPath())
                             .filePath(locale + QStringLiteral(".bdic"));
    QVERIFY(QFileInfo::exists(path));                  // saved (test-mode dir)
    QFile::remove(path);
  }

  void recordsNotInManifest() {
    clearState();
    // A manifest that cannot match the system locale (a made-up code), so the
    // "not in manifest" stop is taken and remembered against the manifest tag.
    const QByteArray manifest =
        R"({"dictionaries":[{"code":"zz_NOPE","size":1,"sha256":"aa"}]})";
    MockDictServer mock;
    const QString url = mock.start(manifest, QByteArray("x"));
    QVERIFY(!url.isEmpty());
    qputenv("WHATLY_DICT_BASE_URL", url.toUtf8());

    DictionaryBootstrap boot;
    QSignalSpy done(&boot, &DictionaryBootstrap::finished);
    boot.beginFetch();
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toBool(), false);
    auto &s = SettingsManager::instance().settings();
    QVERIFY(s.value(QStringLiteral("dictionaries/systemFetchNotInManifest"))
                .toBool());
    QVERIFY(!s.value(QStringLiteral("dictionaries/systemFetchManifestTag"))
                 .toString()
                 .isEmpty());
  }

  void givesUpWithoutNetwork() {
    qputenv("WHATLY_DICT_TEST_FAST", "1");           // tiny backoff
    qputenv("WHATLY_DICT_BASE_URL", "http://127.0.0.1:1"); // nothing listening
    DictionaryBootstrap boot;
    QSignalSpy done(&boot, &DictionaryBootstrap::finished);
    boot.beginFetch();
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toBool(), false);     // gave up, cleanly
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The Cloud API send path, pointed at the mock server via WHATLY_CLOUD_API_BASE:
// text, template and media sends, their API-error, network-error, unconfigured
// and unreadable-file branches. No request ever reaches Meta.
class TstCloudApiNet : public QObject {
  Q_OBJECT
private slots:
  void init() {
    CloudApi::setPhoneNumberId(QStringLiteral("12345"));
    CloudApi::setAccessToken(QStringLiteral("tok"));
    CloudApi::setApiVersion(QStringLiteral("v21.0"));
  }
  void cleanup() {
    qunsetenv("WHATLY_CLOUD_API_BASE");
    CloudApi::setPhoneNumberId(QString());
    CloudApi::setAccessToken(QString());
  }

  void sendTextSucceeds() {
    MockHttpServer mock;
    qputenv("WHATLY_CLOUD_API_BASE",
            mock.start(R"({"messages":[{"id":"wamid.1"}]})").toUtf8());
    const CloudApi::Result r = CloudApi::sendText(QStringLiteral("34600000000"),
                                                  QStringLiteral("hola"));
    QVERIFY(r.ok);
    QCOMPARE(r.messageId, QStringLiteral("wamid.1"));
  }

  void sendTextReportsApiError() {
    MockHttpServer mock;
    qputenv("WHATLY_CLOUD_API_BASE",
            mock.start(R"({"error":{"message":"Bad token"}})").toUtf8());
    const CloudApi::Result r = CloudApi::sendText(QStringLiteral("34600000000"),
                                                  QStringLiteral("hola"));
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QStringLiteral("Bad token"));
  }

  void sendTextNetworkError() {
    qputenv("WHATLY_CLOUD_API_BASE", "http://127.0.0.1:1");
    const CloudApi::Result r = CloudApi::sendText(QStringLiteral("34600000000"),
                                                  QStringLiteral("hola"));
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
  }

  void sendUnconfiguredFails() {
    CloudApi::setAccessToken(QString()); // now not configured
    QVERIFY(!CloudApi::sendText(QStringLiteral("34600000000"),
                                QStringLiteral("hola"))
                 .ok);
    QVERIFY(!CloudApi::sendTemplate(QStringLiteral("34600000000"),
                                    QStringLiteral("t"), QStringLiteral("en"),
                                    {})
                 .ok);
    QVERIFY(!CloudApi::sendMediaFile(QStringLiteral("34600000000"),
                                     QStringLiteral("/no/file"), QString())
                 .ok);
  }

  void sendTemplateSucceeds() {
    MockHttpServer mock;
    qputenv("WHATLY_CLOUD_API_BASE",
            mock.start(R"({"messages":[{"id":"wamid.t"}]})").toUtf8());
    const CloudApi::Result r = CloudApi::sendTemplate(
        QStringLiteral("34600000000"), QStringLiteral("hello_world"),
        QStringLiteral("en_US"), {QStringLiteral("Ada")});
    QVERIFY(r.ok);
  }

  void sendMediaFileSucceedsUploadsThenSends() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/photo.png");
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("PNGDATA");
    }
    // One body serves both requests: the upload reads "id", the send reads
    // "messages".
    MockHttpServer mock;
    qputenv("WHATLY_CLOUD_API_BASE",
            mock.start(R"({"id":"media123","messages":[{"id":"wamid.m"}]})")
                .toUtf8());
    QVERIFY(CloudApi::sendMediaFile(QStringLiteral("34600000000"), path,
                                    QStringLiteral("cap"))
                .ok);
  }

  void sendMediaFileUnreadableFileFails() {
    const CloudApi::Result r = CloudApi::sendMediaFile(
        QStringLiteral("34600000000"), QStringLiteral("/no/such/file.png"),
        QString());
    QVERIFY(!r.ok);
  }

  void sendMediaFileUploadFailure() {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/x.png");
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("X");
    }
    MockHttpServer mock; // no media id in the response → upload is treated failed
    qputenv("WHATLY_CLOUD_API_BASE",
            mock.start(R"({"error":{"message":"upload denied"}})").toUtf8());
    QVERIFY(!CloudApi::sendMediaFile(QStringLiteral("34600000000"), path,
                                     QString())
                 .ok);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// The update check, pointed at the mock via WHATLY_UPDATE_URL: an available
// release, up-to-date, malformed JSON and a network error — plus the disabled
// and recently-checked early returns. No request ever reaches GitHub.
class TstUpdateCheckNet : public QObject {
  Q_OBJECT
private slots:
  void cleanup() { qunsetenv("WHATLY_UPDATE_URL"); }

  void reportsUpdateAvailable() {
    MockHttpServer mock;
    qputenv("WHATLY_UPDATE_URL",
            mock.start(R"({"tag_name":"v99.9.9","html_url":"rel"})")
                .toUtf8());
    UpdateChecker c;
    QSignalSpy up(&c, &UpdateChecker::updateAvailable);
    c.check(true);
    QVERIFY(up.wait(5000));
    QCOMPARE(up.first().at(0).toString(), QStringLiteral("v99.9.9"));
  }

  void reportsUpToDate() {
    MockHttpServer mock;
    qputenv("WHATLY_UPDATE_URL",
            mock.start(R"({"tag_name":"0.0.0","html_url":"rel"})")
                .toUtf8());
    UpdateChecker c;
    QSignalSpy same(&c, &UpdateChecker::upToDate);
    c.check(true);
    QVERIFY(same.wait(5000));
  }

  void reportsBadJson() {
    MockHttpServer mock;
    qputenv("WHATLY_UPDATE_URL", mock.start("not json").toUtf8());
    UpdateChecker c;
    QSignalSpy fail(&c, &UpdateChecker::checkFailed);
    c.check(true);
    QVERIFY(fail.wait(5000));
  }

  void reportsNetworkError() {
    qputenv("WHATLY_UPDATE_URL", "http://127.0.0.1:1");
    UpdateChecker c;
    QSignalSpy fail(&c, &UpdateChecker::checkFailed);
    c.check(true);
    QVERIFY(fail.wait(5000));
  }

  void respectsDisabledAndRateLimit() {
    auto &s = SettingsManager::instance().settings();
    const QVariant savedEnabled = s.value(QStringLiteral("checkForUpdates"));
    const QVariant savedLast = s.value(QStringLiteral("update/lastCheckMs"));

    // Disabled: an unforced check does nothing.
    UpdateChecker::setEnabled(false);
    {
      UpdateChecker c;
      QSignalSpy any(&c, &UpdateChecker::checkFailed);
      c.check(false);
      QVERIFY(any.count() == 0);
    }
    // Enabled but checked a moment ago: the rate limit skips it.
    UpdateChecker::setEnabled(true);
    s.setValue(QStringLiteral("update/lastCheckMs"),
               QDateTime::currentMSecsSinceEpoch());
    {
      UpdateChecker c;
      QSignalSpy any(&c, &UpdateChecker::checkFailed);
      c.check(false);
      QVERIFY(any.count() == 0);
    }
    s.setValue(QStringLiteral("checkForUpdates"), savedEnabled);
    s.setValue(QStringLiteral("update/lastCheckMs"), savedLast);
  }
};

int main(int argc, char *argv[]) {
  // Keep the (headless) QWebEngineProfile used by the install() test happy on CI
  // runners: no sandbox, no GPU. Must be set before QApplication.
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox --disable-gpu");
  // Performance:: and NetworkProxy:: keep a machine-wide store that ignores both
  // the application name and QStandardPaths test mode — on Windows it is the
  // registry, which test mode does not redirect at all. Without this the proxy
  // tests below leave their fixture host, port, user and password in the
  // developer's real configuration.
  qputenv("WHATLY_SETTINGS_APP", "whatly-test");
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
  QCoreApplication::setApplicationName(QStringLiteral("whatly-test"));
  // Keep test settings out of the real config.
  QStandardPaths::setTestModeEnabled(true);

  int status = 0;
  // qExec returns the number of failed functions, but prints the detail to
  // stdout — which is lost whenever the suite runs somewhere without a console
  // (CI logs that capture only stderr, IDE runners, ctest on Windows). Naming
  // the failing class on stderr costs nothing and turns "logic failed" into a
  // place to look.
  auto run = [&](QObject *obj) {
    const int failed = QTest::qExec(obj, argc, argv);
    if (failed != 0)
      fprintf(stderr, "TEST CLASS FAILED: %s (%d function(s))\n",
              obj->metaObject()->className(), failed);
    status |= failed;
  };
  { TstUtils t;               run(&t); }
  { TstScriptLiterals t;      run(&t); }
  { TstDictionaryManager t;   run(&t); }
  { TstUtilsMore t;           run(&t); }
  { TstCommon t;              run(&t); }
  { TstDebugLog t;            run(&t); }
  { TstMessaging t;           run(&t); }
  { TstMessageTemplates t;    run(&t); }
  { TstAutoReply t;           run(&t); }
  { TstCloudApi t;            run(&t); }
  { TstLocalApi t;            run(&t); }
  { TstLocalApiServer t;      run(&t); }
  { TstCloudWebhook t;        run(&t); }
  { TstIdenticons t;          run(&t); }
  { TstTheme t;               run(&t); }
  { TstSettingsSearch t;      run(&t); }
  { TstDictionaries t;        run(&t); }
  { TstSunclock t;            run(&t); }
  { TstScheduled t;           run(&t); }
  { TstScheduledPersistence t; run(&t); }
  { TstScripts t;             run(&t); }
  { TstCustomCss t;           run(&t); }
  { TstCustomJs t;            run(&t); }
  { TstChatWallpaper t;       run(&t); }
  { TstZoom t;                run(&t); }
  { TstPerformance t;         run(&t); }
  { TstTrayIcon t;            run(&t); }
  { TstDropAttach t;          run(&t); }
  { TstDropResolve t;         run(&t); }
  { TstDropReader t;          run(&t); }
  { TstFlatpakManifest t;     run(&t); }
  { TstChatNav t;             run(&t); }
  { TstAccountTabBar t;       run(&t); }
  { TstShortcuts t;           run(&t); }
  { TstBackup t;              run(&t); }
  { TstSessionBackup t;       run(&t); }
  { TstScreenLock t;          run(&t); }
  { TstQuickReply t;          run(&t); }
  { TstFocusMode t;           run(&t); }
  { TstCannedResponses t;     run(&t); }
  { TstHdMedia t;             run(&t); }
  { TstUndoSend t;            run(&t); }
  { TstTranslator t;          run(&t); }
  { TstChatExport t;          run(&t); }
  { TstAiAssistant t;         run(&t); }
  { TstOllama t;              run(&t); }
  { TstPassLock t;            run(&t); }
  { TstStorageInfo t;         run(&t); }
  { TstUpdateCheck t;         run(&t); }
  { TstFuzzy t;               run(&t); }
  { TstNotificationRules t;   run(&t); }
  { TstNetworkProxy t;        run(&t); }
  { TstAutostart t;           run(&t); }
  { TstPortalNotification t;  run(&t); }
  { TstSetupWizard t;         run(&t); }
  { TstScriptInstall t;       run(&t); }
  { TstNetworkClients t;      run(&t); }
  { TstDictionaryManagerNet t; run(&t); }
  { TstDictionaryBootstrapNet t; run(&t); }
  { TstCloudApiNet t;         run(&t); }
  { TstUpdateCheckNet t;      run(&t); }
  { TstWidgets t;             run(&t); }
  { TstFileOps t;             run(&t); }
  { TstScheduledDue t;        run(&t); }
  { TstDictionaryManagerMore t; run(&t); }
  { TstChatExportMore t;      run(&t); }
  { TstUtilsCoverage t;       run(&t); }
  { TstMoreCoverage t;        run(&t); }
  { TstMiscCoverage t;        run(&t); }
  // Profile-mutating test runs last so it doesn't disturb the others.
  { TstAppProfile t;          run(&t); }
  { TstAppProfileArgs t;      run(&t); }
  return status;
}

#include "tst_logic.moc"
