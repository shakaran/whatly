#include <QApplication>
#include <QFont>
#include <QSocketNotifier>

#include "performance.h"
#include "networkproxy.h"
#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>
#include <QtWidgets>
#include <QtWebEngineCore>

#include "common.h"
#include "appprofile.h"
#include "debuglog.h"
#include "def.h"
#include "dictionaries.h"
#include "mainwindow.h"
#include "settingsmanager.h"
#include "webengineprofilemanager.h"
#include <singleapplication.h>

static bool copyDirRecursively(const QString &from, const QString &to) {
  QDir src(from);
  if (!src.exists())
    return false;
  if (!QDir().mkpath(to))
    return false;

  const auto entries = src.entryInfoList(
      QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  for (const QFileInfo &entry : entries) {
    const QString target = to + QLatin1Char('/') + entry.fileName();
    if (entry.isDir())
      copyDirRecursively(entry.absoluteFilePath(), target);
    else if (!QFile::exists(target))
      QFile::copy(entry.absoluteFilePath(), target);
  }
  return true;
}

// Upstream stored user data under the "org.keshavnrj.ubuntu" organisation; this
// fork uses "shakaran", which moves every QStandardPaths location — including
// the WebEngine profile that holds the logged-in WhatsApp session. Copy the
// legacy directories over on first run so existing installs keep their settings
// and stay linked instead of being silently logged out. The originals are left
// untouched (copied, not moved), so an older build still works.
static void migrateLegacyUserData() {
  const QString legacyOrg = QStringLiteral("org.keshavnrj.ubuntu");
  const QString currentOrg = QApplication::organizationName();
  if (currentOrg.isEmpty() || currentOrg == legacyOrg)
    return;

  const auto migrate = [&](QStandardPaths::StandardLocation location) {
    const QString base = QStandardPaths::writableLocation(location);
    if (base.isEmpty())
      return;
    const QString legacy = base + QLatin1Char('/') + legacyOrg;
    const QString current = base + QLatin1Char('/') + currentOrg;
    // Already migrated (or a fresh install), or nothing to carry over.
    if (QDir(current).exists() || !QDir(legacy).exists())
      return;
    qInfo() << "Migrating legacy user data:" << legacy << "->" << current;
    copyDirRecursively(legacy, current);
  };

  migrate(QStandardPaths::GenericConfigLocation); // settings
  migrate(QStandardPaths::GenericDataLocation);   // WebEngine profile / session
}

// Copy one application's user data to another within the current organisation —
// the settings file(s) and the WebEngine profile directory that holds the
// WhatsApp session. Used to bridge the WhatSie → whatly rename: the app name is
// the leaf of every QStandardPaths location, so without this the new name would
// come up with empty settings and logged out. Copied, never moved, and an
// existing file at the destination is left untouched, so it is safe to run more
// than once and an older build still works from the originals.
//
// Returns a description of each copy for reporting; with dryRun the copies are
// only described, not performed (what --migrate-from --dry-run previews).
static QStringList migrateApp(const QString &oldApp, const QString &newApp,
                              bool dryRun) {
  QStringList report;
  const QString org = QApplication::organizationName();
  if (oldApp.isEmpty() || newApp.isEmpty() || oldApp == newApp || org.isEmpty())
    return report;

  // Settings are one file per account: <config>/<org>/<app>.conf, with a
  // "-<suffix>" before the extension for named accounts. Rename the app prefix
  // on each, so WhatSie.conf → whatly.conf and WhatSie-work.conf → whatly-work.conf.
  const QString cfgDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
      QLatin1Char('/') + org;
  QDir cd(cfgDir);
  if (cd.exists()) {
    const QStringList confs =
        cd.entryList({oldApp + QStringLiteral("*.conf")}, QDir::Files);
    for (const QString &f : confs) {
      const QString target = newApp + f.mid(oldApp.length());
      if (!cd.exists(f) || cd.exists(target))
        continue; // never overwrite an existing new-layout file
      report << cd.filePath(f) + QStringLiteral(" → ") + cd.filePath(target);
      if (!dryRun)
        QFile::copy(cd.filePath(f), cd.filePath(target));
    }
  }

  // The profile — session, local storage, cache metadata — is a directory:
  // <data>/<org>/<app>. A single copy carries every account nested inside it.
  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      QLatin1Char('/') + org;
  const QString oldData = dataDir + QLatin1Char('/') + oldApp;
  const QString newData = dataDir + QLatin1Char('/') + newApp;
  if (QDir(oldData).exists() && !QDir(newData).exists()) {
    report << oldData + QStringLiteral(" → ") + newData;
    if (!dryRun)
      copyDirRecursively(oldData, newData);
  }
  return report;
}

// The automatic first-run bridge for the WhatSie → whatly rename.
static void migrateAppRename() {
  const QStringList done =
      migrateApp(QStringLiteral("WhatSie"), QApplication::applicationName(),
                 /*dryRun=*/false);
  for (const QString &line : done)
    qInfo() << "Migrating user data:" << line;
}

// The optional manual migration flag, handled here — early, before anything
// reads or writes settings — so --dry-run genuinely touches nothing and a real
// run is not pre-empted by the automatic first-run copy. Returns an exit code
// when the flag was given (the app should stop after), or -1 to carry on.
//   whatly --migrate-from=whatsie            copy the old install's data in
//   whatly --migrate-from=whatsie --dry-run  show what that would copy
static int runCliMigration(int argc, char *argv[]) {
  bool requested = false, dryRun = false;
  QString from = QStringLiteral("whatsie");
  for (int i = 1; i < argc; ++i) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == QLatin1String("--dry-run")) {
      dryRun = true;
    } else if (a == QLatin1String("--migrate-from")) {
      requested = true; // value, if any, is the next argument
      if (i + 1 < argc) {
        const QString next = QString::fromLocal8Bit(argv[i + 1]);
        if (!next.startsWith(QLatin1Char('-')))
          from = next;
      }
    } else if (a.startsWith(QLatin1String("--migrate-from="))) {
      requested = true;
      from = a.section(QLatin1Char('='), 1);
    }
  }
  if (!requested)
    return -1;

  // Accept the friendly fork name ("whatsie") or a raw application name.
  const QString oldApp =
      from.compare(QStringLiteral("whatsie"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("WhatSie")
          : from;

  const QStringList lines =
      migrateApp(oldApp, QApplication::applicationName(), dryRun);

  QTextStream out(stdout);
  if (lines.isEmpty()) {
    out << QObject::tr("Nothing to migrate from \"%1\" — already migrated, or "
                       "no data found there.")
               .arg(from)
        << '\n';
  } else {
    out << (dryRun ? QObject::tr("Would copy:") : QObject::tr("Copied:")) << '\n';
    for (const QString &l : lines)
      out << "  " << l << '\n';
    if (dryRun)
      out << QObject::tr("Run again without --dry-run to perform the copy.")
          << '\n';
  }
  return 0;
}

// `whatly --unread` prints the running instance's unread total (per profile) and
// exits — for panels, scripts and status bars. The running app keeps the count
// in a runtime file (see MainWindow::updateTrayUnread); this just reads it, so
// it prints 0 when nothing is running. Returns an exit code when handled, or -1.
static int runUnreadQuery(int argc, char *argv[]) {
  bool requested = false;
  for (int i = 1; i < argc; ++i) {
    const QString a = QString::fromLocal8Bit(argv[i]);
    if (a == QLatin1String("--unread") || a == QLatin1String("-u"))
      requested = true;
  }
  if (!requested)
    return -1;

  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty())
    dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QFile f(dir + QStringLiteral("/whatly-unread") + AppProfile::suffix());
  QString value = QStringLiteral("0");
  if (f.open(QIODevice::ReadOnly))
    value = QString::fromLatin1(f.readAll()).trimmed();
  QTextStream(stdout) << (value.isEmpty() ? QStringLiteral("0") : value) << '\n';
  return 0;
}

// Permission types we had no prompt for were denied outright and the denial was
// written to the settings, so they stayed denied forever even once we learned
// how to ask. Clipboard reads were the casualty: WhatsApp Web could not read an
// image out of the clipboard, and pasting failed with "1 image you tried adding
// has no content". Drop those stored denials once so they are asked properly.
// A user who then answers "no" keeps that answer — this only clears the ones
// nobody was ever asked about.
static void clearSilentlyDeniedPermissions() {
  QSettings &s = SettingsManager::instance().settings();
  if (s.value(QStringLiteral("permissionPromptsFixed"), false).toBool())
    return;

  const QList<QWebEnginePermission::PermissionType> previouslyUnprompted = {
      QWebEnginePermission::PermissionType::ClipboardReadWrite,
      QWebEnginePermission::PermissionType::LocalFontsAccess,
  };

  s.beginGroup(QStringLiteral("permissions"));
  for (const auto type : previouslyUnprompted) {
    const QString key = QString::number(static_cast<int>(type));
    if (s.contains(key) && !s.value(key).toBool()) {
      qInfo() << "Clearing permission denial that was never asked for:" << key;
      s.remove(key);
    }
  }
  s.endGroup();
  s.setValue(QStringLiteral("permissionPromptsFixed"), true);
}

// The theme used to be stored as whatever the combo box said, and the combo box
// is translated — so running the app in Spanish wrote windowTheme=claro, and
// every comparison against "dark" then failed for good: the app could switch to
// light and never back. The setting is keyed on the entry's position now, but a
// value written by an older build has to be repaired, and there is no way to
// know which language it was in. Anything unrecognised falls back to light,
// which is the default anyway.
static void normalizeWindowTheme() {
  QSettings &s = SettingsManager::instance().settings();
  const QString theme = s.value(QStringLiteral("windowTheme")).toString();
  if (theme.isEmpty() || theme == QLatin1String("dark") ||
      theme == QLatin1String("light"))
    return;

  qInfo() << "Repairing a theme setting written in the display language:"
          << theme << "-> light";
  s.setValue(QStringLiteral("windowTheme"), QStringLiteral("light"));
}

// The tr() strings were never actually translated: no QTranslator was ever
// installed and the .ts files were not even compiled, so the Italian
// translation in the tree had never once been used. Load one for the system
// locale, falling back to English when there is none — which is every locale
// but Italian for now.
//
// Note this only covers Whatly's own interface. The language of the chats
// themselves is WhatsApp Web's, chosen by WhatsApp from the account and the
// browser locale; the app neither sets nor can override it.
static void installTranslations() {
  // An explicit choice wins over the system locale — the whole point of the
  // request behind this: there was no way to pick a language at all. Empty
  // means "follow the system".
  const QString chosen = SettingsManager::instance()
                             .settings()
                             .value(QStringLiteral("language"))
                             .toString();
  const QLocale locale =
      chosen.isEmpty() ? QLocale::system() : QLocale(chosen);

  // Qt's own strings first (standard dialogs, shortcuts, and so on).
  auto *qtTranslator = new QTranslator(qApp);
  if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                         QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    qApp->installTranslator(qtTranslator);
  }

  // Then Whatly's. qt_add_translations compiles src/i18n/<name>.ts into
  // :/i18n/<name>.qm, so try the full locale before the bare language.
  auto *appTranslator = new QTranslator(qApp);
  const QStringList candidates = {
      locale.name(),                      // it_IT
      locale.name().section('_', 0, 0),   // it
  };
  for (const QString &candidate : candidates) {
    if (appTranslator->load(QStringLiteral(":/i18n/%1.qm").arg(candidate))) {
      qApp->installTranslator(appTranslator);
      qInfo() << "Loaded interface translation for" << candidate;
      return;
    }
  }
}

// Must run before QApplication is created so Qt WebEngine picks these up.
static void setChromiumFlags() {
  // A user who sets the variable themselves takes full control; don't touch it.
  if (!qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
    return;

  // The always-on base: never-wanted browser cruft, plus (on Linux) no sandbox.
  // The GPU / process-model / WebRTC / JS-heap switches come from the user's
  // Performance settings (Performance::chromiumFlagFragment), which default to
  // the historical behaviour — GPU off on Linux — but can now be tuned or turned
  // back on. The sandbox stays on for Windows.
#ifdef Q_OS_WIN
  QString flags = QStringLiteral(
      "--disable-translate --disable-extensions "
      "--disable-component-update --disable-default-apps");
#else
  QString flags = QStringLiteral(
      "--disable-translate --disable-extensions --disable-component-update "
      "--disable-default-apps --no-sandbox");
#endif
  if (const QString perf = Performance::chromiumFlagFragment(); !perf.isEmpty())
    flags += QLatin1Char(' ') + perf;
#ifdef QT_DEBUG
  flags.prepend(QStringLiteral("--remote-debugging-port=9421 "));
#endif

  // A user-set interface scale (Settings) feeds the same QT_SCALE_FACTOR path,
  // but only when the environment did not already set one (an explicit env var
  // always wins). Must happen before QApplication reads QT_SCALE_FACTOR.
  if (qEnvironmentVariable("QT_SCALE_FACTOR").isEmpty()) {
    const double stored = Performance::interfaceScaleFactor();
    if (stored > 0.0)
      qputenv("QT_SCALE_FACTOR", QByteArray::number(stored));
  }

  // #203: scale the web content to match the widget scale, so a single
  // QT_SCALE_FACTOR gives a HiDPI/4K display a bigger UI *and* bigger page
  // chrome instead of leaving WhatsApp Web tiny next to a scaled-up window.
  const QString scale = qEnvironmentVariable("QT_SCALE_FACTOR");
  if (!scale.isEmpty()) {
    bool ok = false;
    scale.toDouble(&ok);
    if (ok)
      flags += QStringLiteral(" --force-device-scale-factor=") + scale;
  }
  // #221: let WhatsApp Web run at the display's full refresh rate instead of
  // Chromium's default cap, for the users who explicitly ask for it.
  if (qEnvironmentVariableIntValue("WHATLY_MAX_FPS") > 0)
    flags += QStringLiteral(" --disable-frame-rate-limit");

  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.toUtf8());
}

int main(int argc, char *argv[]) {
  DebugLog::install();   // before anything can log

  // Which account this is has to be settled before anything else: it feeds the
  // single-instance key below, the settings file, and the WebEngine storage,
  // all of which are fixed the moment they are first touched. Parsed straight
  // from argv because QCommandLineParser needs a QApplication that does not
  // exist yet.
  AppProfile::initFromArgs(argc, argv);

  // Qt6 on Linux routes qDebug/qWarning to journald when the process is not
  // attached to a TTY (e.g. when launched from an IDE).  Force stderr output
  // so the IDE Run console always captures debug logs.
#ifdef QT_DEBUG
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif

  setChromiumFlags();

  // The account id is folded into SingleApplication's instance key (it hashes
  // userData into the key), so two accounts are two primary instances that do
  // not see each other, while a second launch of the *same* account still
  // collapses into the running one. The default account passes nothing, keeping
  // its key byte-for-byte what it was.
  SingleApplication instance(argc, argv, true, SingleApplication::Mode::User,
                             1000, AppProfile::id());
  instance.setQuitOnLastWindowClosed(false);
  instance.setWindowIcon(themeIcon("whatly", ":/icons/app/icon-64.png"));
  // The machine name is lowercase — it is the leaf of every QStandardPaths
  // location (~/.local/share/shakaran/whatly, ~/.config/shakaran/whatly.conf)
  // and of the settings file. The human-facing name, shown in window titles and
  // the About box, is set separately so those read "Whatly", not "whatly".
  QApplication::setApplicationName("whatly");
  QApplication::setApplicationDisplayName("Whatly");
  QApplication::setDesktopFileName("net.shakaran.whatly");
  QApplication::setOrganizationDomain("net.shakaran");
  QApplication::setOrganizationName("shakaran");
  QApplication::setApplicationVersion(VERSIONSTR);

  // Install the configured network proxy before any account profile opens a
  // connection (Qt WebEngine honours the application-wide proxy).
  NetworkProxy::applyToApplication();

  // The manual migration flag is handled before any settings are read or
  // written, so --dry-run stays side-effect free and a real copy is not
  // pre-empted by the automatic first-run migration below. Registered in the
  // parser further down only so it appears in --help.
  if (const int rc = runCliMigration(argc, argv); rc >= 0)
    return rc;

  // `--unread` just reads a runtime file, so answer it here and exit without
  // spinning up the GUI or connecting to a running instance.
  if (const int rc = runUnreadQuery(argc, argv); rc >= 0)
    return rc;

  // Carry user data forward BEFORE anything reads settings. Two layout changes
  // have to be bridged: the organisation rename (keshavnrj → shakaran) and the
  // application rename (WhatSie → whatly), each of which moves every
  // QStandardPaths location — settings and the WebEngine profile that holds the
  // WhatsApp session alike. QSettings caches whatever it reads the first time it
  // is opened, so the copy must be in place before that first read, which
  // installTranslations() below triggers.
  migrateLegacyUserData();
  migrateAppRename();

  installTranslations();
  clearSilentlyDeniedPermissions();
  normalizeWindowTheme();

  // #76: apply the user's chosen interface font size (menus, dialogs, settings).
  // 0/unset leaves Qt's default alone. WhatsApp Web's own text is unaffected;
  // that is the page zoom's job.
  if (const int ptSize = SettingsManager::instance()
                             .settings()
                             .value("interfaceFontSize", 0)
                             .toInt();
      ptSize > 0) {
    QFont f = qApp->font();
    f.setPointSize(ptSize);
    qApp->setFont(f);
  }

  // Qt reads QTWEBENGINE_DICTIONARIES_PATH once, when the profile is
  // constructed, so this has to happen before anything touches the profile.
  // Without it Chromium looks beside the executable, finds nothing, and the
  // spell checker silently does nothing at all — which is how it shipped.
  const QString dictionaries = Dictionaries::dictionaryPath();
  if (dictionaries.isEmpty())
    qWarning() << "No spell-check dictionaries found; spell checking is off";
  else
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dictionaries.toLocal8Bit());


  QCommandLineParser parser;
  parser.setApplicationDescription(
      QObject::tr("Feature rich WhatsApp web client based on Qt WebEngine"));

  QList<QCommandLineOption> secondaryInstanceCLIOptions;

  QCommandLineOption showCLIHelpOption(
      QStringList() << "h"
                    << "help",
      QObject::tr("Displays help on commandline options"));

  QCommandLineOption openSettingsOption(
      QStringList() << "s"
                    << "open-settings",
      QObject::tr("Opens Settings dialog in a running instance of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption lockAppOption(QStringList() << "l"
                                                 << "lock-app",
                                   QObject::tr("Locks a running instance of ") +
                                       QApplication::applicationDisplayName());

  QCommandLineOption openAboutOption(
      QStringList() << "i"
                    << "open-about",
      QObject::tr("Opens About dialog in a running instance of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption openScheduledOption(
      QStringList() << "open-scheduled",
      QObject::tr("Opens the scheduled messages dialog in a running instance "
                  "of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption toggleThemeOption(
      QStringList() << "t"
                    << "toggle-theme",
      QObject::tr(
          "Toggle between dark & light theme in a running instance of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption reloadAppOption(
      QStringList() << "r"
                    << "reload-app",
      QObject::tr("Reload the app in a running instance of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption newChatOption(
      QStringList() << "n"
                    << "new-chat",
      QObject::tr("Open new chat prompt in a running instance of ") +
          QApplication::applicationDisplayName());

  QCommandLineOption buildInfoOption(QStringList() << "b"
                                                   << "build-info",
                                     "Shows detailed current build infomation");

  // Already consumed by AppProfile::initFromArgs() before QApplication existed;
  // registered here only so it shows up in --help and does not trip the parser
  // as an unknown option.
  QCommandLineOption profileOption(
      QStringList() << "p"
                    << "profile",
      QObject::tr("Run as a separate account with its own session and settings, "
                  "in its own window"),
      QStringLiteral("name"));

  QCommandLineOption showAppWindowOption(
      QStringList() << "w"
                    << "show-window",
      QObject::tr("Show main window of running instance of ") +
          QApplication::applicationDisplayName());

  // Handled early by runCliMigration() before settings are touched; registered
  // here only so they are documented in --help and not rejected as unknown.
  QCommandLineOption migrateFromOption(
      QStringList() << "migrate-from",
      QObject::tr("Copy settings and the logged-in session from a previous "
                  "install (e.g. the older \"whatsie\" build) into this one, "
                  "then exit"),
      QStringLiteral("name"));

  QCommandLineOption dryRunOption(
      QStringList() << "dry-run",
      QObject::tr("With --migrate-from, only report what would be copied"));

  parser.addOption(showCLIHelpOption);
  parser.addVersionOption();
  parser.addOption(buildInfoOption);
  parser.addOption(showAppWindowOption);
  parser.addOption(openSettingsOption);
  parser.addOption(lockAppOption);
  parser.addOption(openAboutOption);
  parser.addOption(openScheduledOption);
  parser.addOption(toggleThemeOption);
  parser.addOption(reloadAppOption);
  parser.addOption(newChatOption);
  parser.addOption(profileOption);
  QCommandLineOption unreadOption(
      QStringList() << "u"
                    << "unread",
      QObject::tr("Print the current unread message count and exit"));

  parser.addOption(migrateFromOption);
  parser.addOption(dryRunOption);
  parser.addOption(unreadOption);

  secondaryInstanceCLIOptions << showAppWindowOption << openSettingsOption
                              << lockAppOption << openAboutOption
                              << openScheduledOption << toggleThemeOption
                              << reloadAppOption << newChatOption;

  parser.process(instance);

  if (parser.isSet(showCLIHelpOption)) {
    parser.showHelp();
  }

  if (parser.isSet(buildInfoOption)) {

    qInfo().noquote()
        << parser.applicationDescription() << "\n"
        << QStringLiteral("version: %1, branch: %2, commit: %3, built_at: %4")
               .arg(VERSIONSTR, GIT_BRANCH, GIT_HASH, BUILD_TIMESTAMP);
    return 0;
  }

  // if secondary instance is invoked
  if (instance.isSecondary()) {
    instance.sendMessage(instance.arguments().join(' ').toUtf8());
    qInfo().noquote() << QApplication::applicationDisplayName() +
                             " is already running with PID: " +
                             QString::number(instance.primaryPid()) +
                             " by USER:"
                      << instance.primaryUser();
    return 0;
  }

  // Initialise the single persistent WebEngine profile before any page is created.
  WebEngineProfileManager::instance();

  MainWindow whatly;

  // else
  QObject::connect(
      &instance, &SingleApplication::receivedMessage, &whatly,
      [&whatly, &secondaryInstanceCLIOptions](int instanceId,
                                               QByteArray message) {
        qInfo().noquote() << "Another instance with PID: " +
                                 QString::number(instanceId) +
                                 ", sent argument: " + message;
        QString messageStr = QString::fromUtf8(message);

        QCommandLineParser p;
        p.addOptions(secondaryInstanceCLIOptions);
        p.parse(QStringList(messageStr.split(" ")));

        if (p.isSet("s")) {
          qInfo() << "cmd:"
                  << "OpenAppSettings";
          whatly.alreadyRunning();
          whatly.showSettings(true);
          return;
        }

        if (p.isSet("l")) {
          qInfo() << "cmd:"
                  << "LockApp";
          whatly.alreadyRunning();
          if (!SettingsManager::instance()
                   .settings()
                   .value("asdfg")
                   .isValid()) {
            whatly.showNotification(
                QApplication::applicationDisplayName(),
                QObject::tr("App lock is not configured, \n"
                            "Please setup the password in the Settings "
                            "first."));
          } else {
            whatly.lockApp();
          }
          return;
        }

        if (p.isSet("i")) {
          qInfo() << "cmd:"
                  << "OpenAppAbout";
          whatly.alreadyRunning();
          whatly.showAbout();
          return;
        }

        if (p.isSet("open-scheduled")) {
          qInfo() << "cmd:"
                  << "OpenScheduledMessages";
          whatly.alreadyRunning();
          whatly.showScheduledMessages();
          return;
        }

        if (p.isSet("t")) {
          qInfo() << "cmd:"
                  << "ToggleAppTheme";
          whatly.alreadyRunning();
          whatly.toggleTheme();
          return;
        }

        if (p.isSet("r")) {
          qInfo() << "cmd:"
                  << "ReloadApp";
          whatly.alreadyRunning();
          whatly.doReload(false, true);
          return;
        }

        if (p.isSet("n")) {
          qInfo() << "cmd:"
                  << "OpenNewChatPrompt";
          whatly.alreadyRunning();
          whatly.newChat(); // TODO: invetigate the crash
          return;
        }

        if (p.isSet("w")) {
          qInfo() << "cmd:"
                  << "ShowAppWindow";
          whatly.alreadyRunning();
          whatly.show();
          return;
        }

        if (messageStr.contains("whatsapp://", Qt::CaseInsensitive)) {
          QString urlStr =
              "whatsapp://" + messageStr.split("whatsapp://").last();
          qInfo() << "cmd:"
                  << "x-schema-handler";
          whatly.loadSchemaUrl(urlStr);
        } else {
          whatly.alreadyRunning();
        }
      });

  foreach (QString argStr, instance.arguments()) {
    if (argStr.contains("whatsapp://")) {
      qInfo() << "cmd:"
              << "x-schema-handler";
      whatly.loadSchemaUrl(argStr);
    }
  }

  if (QSystemTrayIcon::isSystemTrayAvailable() &&
      SettingsManager::instance()
          .settings()
          .value("startMinimized", false)
          .toBool()) {
    whatly.runMinimized();
  } else {
    whatly.show();
  }

#ifdef Q_OS_UNIX
  // Quit gracefully on SIGTERM (a session manager, `kill`, or systemd) instead
  // of being torn down abruptly — a clean shutdown, and in a coverage build it
  // lets gcov flush on exit. The signal handler only pokes a socketpair; the
  // actual quit happens back in the event loop, which is the Qt-safe pattern.
  {
    static int sigFd[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) == 0) {
      auto *sn = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &instance);
      QObject::connect(sn, &QSocketNotifier::activated, qApp,
                       [] { qApp->quit(); });
      struct sigaction sa;
      sa.sa_handler = [](int) {
        const char c = 1;
        const ssize_t r = ::write(sigFd[0], &c, 1);
        (void)r;
      };
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      ::sigaction(SIGTERM, &sa, nullptr);
    }
  }
#endif

  return instance.exec();
}
