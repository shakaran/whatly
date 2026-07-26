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
#include <QTimer>
#include <QTranslator>
#include <QtWidgets>
#include <QtWebEngineCore>

#include "common.h"
#include "appprofile.h"
#include "debuglog.h"
#include "def.h"
#include "dictionaries.h"
#include "mainwindow.h"
#include "autoreply.h"
#include "cloudapi.h"
#include "localapi.h"
#include "cloudwebhook.h"
#include "messaging.h"
#include "messagetemplates.h"
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

  // Detect a previous launch that crashed before WhatsApp Web loaded, so the
  // Chromium flags built next can escalate to safer rendering (issue #3).
  Performance::evaluateStartup();

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

  // Now that the app/org names (and thus the data path) are set, persist
  // Chromium's own fd-2 output to a file for bug reports. Must be before Qt
  // WebEngine starts, which the profile creation below is the first to do.
  DebugLog::captureNativeStderr();

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

  // Send a message from the command line to the already-running instance of
  // this profile (issue: send by command / API).
  QCommandLineOption sendOption(
      QStringList() << "send",
      QObject::tr("Send a message via the running instance, then exit (needs "
                  "--to and --message)"));
  QCommandLineOption toOption(
      QStringList() << "to",
      QObject::tr("Recipient for --send: a phone number (international), a "
                  "group id, or a contact name"),
      QStringLiteral("recipient"));
  QCommandLineOption messageOption(
      QStringList() << "message",
      QObject::tr("Message text for --send"), QStringLiteral("text"));
  QCommandLineOption fileOption(
      QStringList() << "file",
      QObject::tr("Attach a file for --send (its --message, if any, becomes the "
                  "caption)"),
      QStringLiteral("path"));
  QCommandLineOption captionOption(
      QStringList() << "caption",
      QObject::tr("Caption for the --file attachment (alias of --message)"),
      QStringLiteral("text"));
  QCommandLineOption backendOption(
      QStringList() << "backend",
      QObject::tr("How --send delivers: 'web' (the running WhatsApp Web "
                  "session) or 'cloud' (Meta WhatsApp Business Cloud API)"),
      QStringLiteral("web|cloud"), QStringLiteral("web"));
  // Reusable message templates (per account).
  QCommandLineOption templateOption(
      QStringList() << "template",
      QObject::tr("Use the saved template of this name as the --send message "
                  "(fill its {{fields}} with --var)"),
      QStringLiteral("name"));
  QCommandLineOption varOption(
      QStringList() << "var",
      QObject::tr("Fill a template field: key=value (repeatable)"),
      QStringLiteral("key=value"));
  QCommandLineOption templateListOption(
      QStringList() << "template-list",
      QObject::tr("List the saved message templates and exit"));
  QCommandLineOption templateSetOption(
      QStringList() << "template-set",
      QObject::tr("Save (or replace) a message template, then exit: name=body"),
      QStringLiteral("name=body"));
  QCommandLineOption templateRemoveOption(
      QStringList() << "template-remove",
      QObject::tr("Delete the saved message template of this name, then exit"),
      QStringLiteral("name"));
  // Auto-reply ("listener") management.
  QCommandLineOption autoReplyOnOption(
      QStringList() << "autoreply-on",
      QObject::tr("Turn auto-reply to incoming messages on, then exit"));
  QCommandLineOption autoReplyOffOption(
      QStringList() << "autoreply-off",
      QObject::tr("Turn auto-reply off, then exit"));
  QCommandLineOption autoReplyListOption(
      QStringList() << "autoreply-list",
      QObject::tr("List the active auto-reply rules (status included) and exit"));
  QCommandLineOption autoReplyFileOption(
      QStringList() << "autoreply-file",
      QObject::tr("Use this JSON file as a source of auto-reply rules, then "
                  "exit (empty to clear)"),
      QStringLiteral("path"));
  // Meta WhatsApp Business Cloud API configuration + template send.
  QCommandLineOption cloudPhoneIdOption(
      QStringList() << "cloud-phone-id",
      QObject::tr("Set the Cloud API phone-number id, then exit"),
      QStringLiteral("id"));
  QCommandLineOption cloudTokenOption(
      QStringList() << "cloud-token",
      QObject::tr("Set the Cloud API access token, then exit (stored in the "
                  "account config)"),
      QStringLiteral("token"));
  QCommandLineOption cloudApiVersionOption(
      QStringList() << "cloud-api-version",
      QObject::tr("Set the Cloud API graph version (e.g. v21.0), then exit"),
      QStringLiteral("version"));
  QCommandLineOption cloudStatusOption(
      QStringList() << "cloud-status",
      QObject::tr("Show whether the Cloud API is configured, then exit"));
  QCommandLineOption cloudTemplateOption(
      QStringList() << "cloud-template",
      QObject::tr("For --send --backend cloud: send this Meta-approved template"),
      QStringLiteral("name"));
  QCommandLineOption cloudLangOption(
      QStringList() << "cloud-lang",
      QObject::tr("Language code for --cloud-template (e.g. es, en_US)"),
      QStringLiteral("code"), QStringLiteral("en_US"));
  QCommandLineOption cloudParamOption(
      QStringList() << "cloud-param",
      QObject::tr("A positional body parameter for --cloud-template (repeatable)"),
      QStringLiteral("value"));
  // Local HTTP API (loopback only) configuration.
  QCommandLineOption localApiOnOption(
      QStringList() << "localapi-on",
      QObject::tr("Enable the local HTTP API, then exit"));
  QCommandLineOption localApiOffOption(
      QStringList() << "localapi-off",
      QObject::tr("Disable the local HTTP API, then exit"));
  QCommandLineOption localApiPortOption(
      QStringList() << "localapi-port",
      QObject::tr("Set the local HTTP API port (default 8590), then exit"),
      QStringLiteral("port"));
  QCommandLineOption localApiTokenOption(
      QStringList() << "localapi-token",
      QObject::tr("Set the local HTTP API bearer token, then exit"),
      QStringLiteral("token"));
  QCommandLineOption localApiStatusOption(
      QStringList() << "localapi-status",
      QObject::tr("Show the local HTTP API configuration, then exit"));
  // Cloud API webhook (incoming messages) configuration.
  QCommandLineOption webhookOnOption(
      QStringList() << "webhook-on",
      QObject::tr("Enable receiving Cloud API webhooks, then exit"));
  QCommandLineOption webhookOffOption(
      QStringList() << "webhook-off",
      QObject::tr("Disable receiving Cloud API webhooks, then exit"));
  QCommandLineOption webhookVerifyTokenOption(
      QStringList() << "webhook-verify-token",
      QObject::tr("Set the Cloud API webhook verify token, then exit"),
      QStringLiteral("token"));
  QCommandLineOption webhookAppSecretOption(
      QStringList() << "webhook-app-secret",
      QObject::tr("Set the Meta app secret for webhook signature checks, then exit"),
      QStringLiteral("secret"));
  QCommandLineOption webhookStatusOption(
      QStringList() << "webhook-status",
      QObject::tr("Show the Cloud API webhook configuration, then exit"));

  parser.addOption(migrateFromOption);
  parser.addOption(dryRunOption);
  parser.addOption(unreadOption);
  parser.addOption(sendOption);
  parser.addOption(toOption);
  parser.addOption(messageOption);
  parser.addOption(fileOption);
  parser.addOption(captionOption);
  parser.addOption(backendOption);
  parser.addOption(templateOption);
  parser.addOption(varOption);
  parser.addOption(templateListOption);
  parser.addOption(templateSetOption);
  parser.addOption(templateRemoveOption);
  parser.addOption(autoReplyOnOption);
  parser.addOption(autoReplyOffOption);
  parser.addOption(autoReplyListOption);
  parser.addOption(autoReplyFileOption);
  parser.addOption(cloudPhoneIdOption);
  parser.addOption(cloudTokenOption);
  parser.addOption(cloudApiVersionOption);
  parser.addOption(cloudStatusOption);
  parser.addOption(cloudTemplateOption);
  parser.addOption(cloudLangOption);
  parser.addOption(cloudParamOption);
  parser.addOption(localApiOnOption);
  parser.addOption(localApiOffOption);
  parser.addOption(localApiPortOption);
  parser.addOption(localApiTokenOption);
  parser.addOption(localApiStatusOption);
  parser.addOption(webhookOnOption);
  parser.addOption(webhookOffOption);
  parser.addOption(webhookVerifyTokenOption);
  parser.addOption(webhookAppSecretOption);
  parser.addOption(webhookStatusOption);

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

  // Template management operates directly on this profile's settings and exits;
  // it needs neither a running instance nor the GUI.
  if (parser.isSet(templateListOption)) {
    QTextStream out(stdout);
    const auto list = MessageTemplates::all();
    for (const auto &t : list)
      out << t.name << '\n';
    return 0;
  }
  if (parser.isSet(templateSetOption)) {
    const QString spec = parser.value(templateSetOption);
    const qsizetype eq = spec.indexOf(QLatin1Char('='));
    if (eq <= 0) {
      QTextStream(stderr)
          << "whatly --template-set: expected name=body\n";
      return 2;
    }
    MessageTemplates::set(spec.left(eq), spec.mid(eq + 1));
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Saved template " << spec.left(eq) << '\n';
    return 0;
  }
  if (parser.isSet(templateRemoveOption)) {
    const QString name = parser.value(templateRemoveOption);
    const bool removed = MessageTemplates::remove(name);
    SettingsManager::instance().settings().sync();
    QTextStream(removed ? stdout : stderr)
        << (removed ? "Removed template " : "No such template: ") << name
        << '\n';
    return removed ? 0 : 1;
  }

  // Auto-reply management operates on this profile's settings and exits.
  if (parser.isSet(autoReplyOnOption) || parser.isSet(autoReplyOffOption)) {
    AutoReply::setEnabled(parser.isSet(autoReplyOnOption));
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Auto-reply is now "
                        << (AutoReply::isEnabled() ? "on" : "off") << '\n';
    return 0;
  }
  if (parser.isSet(autoReplyFileOption)) {
    AutoReply::setRulesFilePath(parser.value(autoReplyFileOption).trimmed());
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Auto-reply rules file set to: "
                        << (AutoReply::rulesFilePath().isEmpty()
                                ? QStringLiteral("(none)")
                                : AutoReply::rulesFilePath())
                        << '\n';
    return 0;
  }
  if (parser.isSet(autoReplyListOption)) {
    QTextStream out(stdout);
    out << "Auto-reply: " << (AutoReply::isEnabled() ? "ON" : "OFF") << '\n';
    const auto rules = AutoReply::activeRules();
    for (const auto &r : rules)
      out << "  [" << AutoReply::matchTypeName(r.type) << (r.enabled ? "" : ", disabled")
          << "] " << r.pattern << " -> " << r.reply << '\n';
    if (rules.isEmpty())
      out << "  (no rules)\n";
    return 0;
  }

  // Cloud API configuration operates on this profile's settings and exits.
  if (parser.isSet(cloudPhoneIdOption) || parser.isSet(cloudTokenOption) ||
      parser.isSet(cloudApiVersionOption)) {
    if (parser.isSet(cloudPhoneIdOption))
      CloudApi::setPhoneNumberId(parser.value(cloudPhoneIdOption));
    if (parser.isSet(cloudTokenOption))
      CloudApi::setAccessToken(parser.value(cloudTokenOption));
    if (parser.isSet(cloudApiVersionOption))
      CloudApi::setApiVersion(parser.value(cloudApiVersionOption));
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Cloud API config updated.\n";
    return 0;
  }
  if (parser.isSet(cloudStatusOption)) {
    QTextStream(stdout)
        << "Cloud API: " << (CloudApi::isConfigured() ? "configured" : "not configured")
        << " (phone-number id " << (CloudApi::phoneNumberId().isEmpty() ? "unset" : "set")
        << ", token " << (CloudApi::accessToken().isEmpty() ? "unset" : "set")
        << ", api " << CloudApi::apiVersion() << ")\n";
    return 0;
  }

  // Local HTTP API configuration operates on this profile's settings and exits.
  // A running instance picks up the change on its next launch (or via Settings).
  if (parser.isSet(localApiOnOption) || parser.isSet(localApiOffOption) ||
      parser.isSet(localApiPortOption) || parser.isSet(localApiTokenOption)) {
    if (parser.isSet(localApiOffOption))
      LocalApi::setEnabled(false);
    if (parser.isSet(localApiOnOption))
      LocalApi::setEnabled(true);
    if (parser.isSet(localApiPortOption)) {
      bool ok = false;
      const int p = parser.value(localApiPortOption).toInt(&ok);
      if (!ok || p <= 0 || p > 65535) {
        QTextStream(stderr) << "whatly --localapi-port: a port in 1..65535 is "
                               "required\n";
        return 2;
      }
      LocalApi::setPort(p);
    }
    if (parser.isSet(localApiTokenOption))
      LocalApi::setToken(parser.value(localApiTokenOption));
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Local API config updated.\n";
    return 0;
  }
  if (parser.isSet(localApiStatusOption)) {
    QTextStream(stdout)
        << "Local API: "
        << (LocalApi::isConfigured() ? "configured" : "not configured") << " ("
        << (LocalApi::isEnabled() ? "enabled" : "disabled") << ", token "
        << (LocalApi::token().isEmpty() ? "unset" : "set") << ", "
        << LocalApi::bindAddress() << ":" << LocalApi::port() << ")\n";
    return 0;
  }

  // Cloud API webhook configuration operates on this profile's settings and
  // exits. A running instance picks it up on its next launch.
  if (parser.isSet(webhookOnOption) || parser.isSet(webhookOffOption) ||
      parser.isSet(webhookVerifyTokenOption) ||
      parser.isSet(webhookAppSecretOption)) {
    if (parser.isSet(webhookOffOption))
      CloudWebhook::setEnabled(false);
    if (parser.isSet(webhookOnOption))
      CloudWebhook::setEnabled(true);
    if (parser.isSet(webhookVerifyTokenOption))
      CloudWebhook::setVerifyToken(parser.value(webhookVerifyTokenOption));
    if (parser.isSet(webhookAppSecretOption))
      CloudWebhook::setAppSecret(parser.value(webhookAppSecretOption));
    SettingsManager::instance().settings().sync();
    QTextStream(stdout) << "Cloud API webhook config updated.\n";
    return 0;
  }
  if (parser.isSet(webhookStatusOption)) {
    QTextStream(stdout)
        << "Cloud API webhook: "
        << (CloudWebhook::isEnabled() ? "enabled" : "disabled")
        << " (verify token "
        << (CloudWebhook::verifyToken().isEmpty() ? "unset" : "set")
        << ", app secret "
        << (CloudWebhook::appSecret().isEmpty() ? "unset" : "set")
        << ", served at " << LocalApi::bindAddress() << ":" << LocalApi::port()
        << "/webhook)\n";
    return 0;
  }

  // `--send` hands a message to the already-running instance of this profile
  // and exits. The web backend needs that instance's logged-in WhatsApp Web
  // session, so there must be one running; the cloud backend will not (Phase 4).
  if (parser.isSet(sendOption)) {
    bool backendOk = false;
    const Messaging::Backend backend =
        Messaging::parseBackend(parser.value(backendOption), &backendOk);
    QTextStream err(stderr);
    if (!backendOk) {
      err << "whatly --send: unknown --backend '" << parser.value(backendOption)
          << "' (use 'web' or 'cloud')\n";
      return 2;
    }
    if (!parser.isSet(toOption) || parser.value(toOption).trimmed().isEmpty()) {
      err << "whatly --send: --to <recipient> is required\n";
      return 2;
    }
    const bool cloudTemplate =
        backend == Messaging::Backend::Cloud && parser.isSet(cloudTemplateOption);
    if (!parser.isSet(messageOption) && !parser.isSet(fileOption) &&
        !parser.isSet(templateOption) && !cloudTemplate) {
      err << "whatly --send: --message, --file or --template is required\n";
      return 2;
    }
    // Resolve a template (per-account) into the message text here in the caller,
    // filling its {{fields}} from --var; any left unfilled is an error.
    QString bodyText;
    if (parser.isSet(templateOption)) {
      const QString tname = parser.value(templateOption);
      if (!MessageTemplates::exists(tname)) {
        err << "whatly --send: no such template: " << tname << '\n';
        return 2;
      }
      bodyText = Messaging::fillTemplate(
          MessageTemplates::body(tname),
          Messaging::parseVars(parser.values(varOption)));
      const QStringList missing = Messaging::templatePlaceholders(bodyText);
      if (!missing.isEmpty()) {
        err << "whatly --send: template '" << tname << "' still needs: "
            << missing.join(QStringLiteral(", "))
            << " (pass them with --var key=value)\n";
        return 2;
      }
    } else {
      bodyText = parser.isSet(captionOption) ? parser.value(captionOption)
                                             : parser.value(messageOption);
    }
    // Resolve and validate the attachment here (in the caller's working
    // directory), so the running instance receives an absolute, checked path.
    QString absFile;
    if (parser.isSet(fileOption)) {
      const QFileInfo fi(parser.value(fileOption));
      if (!fi.exists() || !fi.isFile()) {
        err << "whatly --send: file not found: " << parser.value(fileOption)
            << '\n';
        return 2;
      }
      if (!fi.isReadable()) {
        err << "whatly --send: file is not readable: " << fi.absoluteFilePath()
            << '\n';
        return 2;
      }
      // The web backend carries the bytes into the page, so cap the size; the
      // Cloud API (Phase 4) will lift this.
      constexpr qint64 kMaxWebAttachmentBytes = 3 * 1024 * 1024;
      if (backend == Messaging::Backend::Web &&
          fi.size() > kMaxWebAttachmentBytes) {
        err << "whatly --send: file is too large for the web backend ("
            << (fi.size() / 1024) << " KiB; max 3072 KiB)\n";
        return 2;
      }
      absFile = fi.absoluteFilePath();
    }

    // The cloud backend sends directly over HTTP — no running instance needed.
    if (backend == Messaging::Backend::Cloud) {
      const Messaging::Recipient rc =
          Messaging::parseRecipient(parser.value(toOption));
      if (rc.kind != Messaging::RecipientKind::PhoneNumber) {
        err << "whatly --send: the cloud backend needs a phone number in --to\n";
        return 2;
      }
      if (!CloudApi::isConfigured()) {
        err << "whatly --send: Cloud API is not configured — set "
               "--cloud-phone-id and --cloud-token first\n";
        return 1;
      }
      CloudApi::Result res;
      if (parser.isSet(cloudTemplateOption))
        res = CloudApi::sendTemplate(rc.value, parser.value(cloudTemplateOption),
                                     parser.value(cloudLangOption),
                                     parser.values(cloudParamOption));
      else if (!absFile.isEmpty())
        res = CloudApi::sendMediaFile(rc.value, absFile, bodyText);
      else
        res = CloudApi::sendText(rc.value, bodyText);
      if (res.ok) {
        QTextStream(stdout)
            << "Sent via Cloud API (message id " << res.messageId << ")\n";
        return 0;
      }
      err << "whatly --send: Cloud API send failed: " << res.error << '\n';
      return 1;
    }

    if (!instance.isSecondary()) {
      err << "whatly --send: no running Whatly for this profile"
          << AppProfile::suffix()
          << " — start it and sign in first (web backend)\n";
      return 1;
    }
    Messaging::SendCommand cmd;
    cmd.backend = backend;
    cmd.to = parser.value(toOption);
    cmd.message = bodyText; // from --template, or --caption/--message
    cmd.file = absFile;
    if (!instance.sendMessage(Messaging::encodeSendCommand(cmd).toUtf8())) {
      err << "whatly --send: could not reach the running instance\n";
      return 1;
    }
    QTextStream(stdout) << "Sent to the running Whatly for delivery to "
                        << cmd.to << '\n';
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

        // A `--send` command arrives as a tagged JSON payload (not argv), so it
        // is decoded before the space-splitting parser below, which would
        // mangle a message containing spaces.
        Messaging::SendCommand sc;
        if (Messaging::decodeSendCommand(messageStr, &sc)) {
          qInfo() << "cmd:" << "SendMessage" << "backend"
                  << Messaging::backendName(sc.backend);
          whatly.commandSend(sc);
          return;
        }

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

  // Arm the start-up crash watch just before we commit to loading the page:
  // markStartupSucceeded() (on the first successful load) disarms it, so if the
  // process dies before that the next launch escalates to safer rendering.
  Performance::armStartupWatch();

  if (QSystemTrayIcon::isSystemTrayAvailable() &&
      SettingsManager::instance()
          .settings()
          .value("startMinimized", false)
          .toBool()) {
    whatly.runMinimized();
  } else {
    whatly.show();
  }

  // If we are here because a previous start crashed before the page loaded, let
  // the user know rendering was dialled back — and how to tune it — once the UI
  // is up. It self-heals: a clean load resets the level back to normal.
  if (Performance::recoveryLevel() > 0) {
    QTimer::singleShot(1500, &whatly, [&whatly] {
      whatly.showNotification(
          QApplication::applicationDisplayName(),
          QObject::tr("Recovered from a start-up crash by switching to safe "
                      "rendering. You can adjust this in Settings → "
                      "Performance."));
    });
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
