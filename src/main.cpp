#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>
#include <QtWidgets>
#include <QtWebEngineCore>

#include "common.h"
#include "def.h"
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

// The tr() strings were never actually translated: no QTranslator was ever
// installed and the .ts files were not even compiled, so the Italian
// translation in the tree had never once been used. Load one for the system
// locale, falling back to English when there is none — which is every locale
// but Italian for now.
//
// Note this only covers WhatSie's own interface. The language of the chats
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

  // Then WhatSie's. qt_add_translations compiles src/i18n/<name>.ts into
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
  if (!qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
    return;
#ifdef Q_OS_WIN
  // On Windows the GPU stays enabled (software rendering is visibly slow),
  // but compositing is kept in software: GPU compositing exhibits
  // stale-frame flicker with Qt WebEngine on Windows (same workaround as
  // e.g. ankitects/anki#4470). Chromium's sandbox works fine on Windows,
  // so it is not disabled here.
#ifdef QT_DEBUG
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
    "--remote-debugging-port=9421 "
          "--disable-gpu-compositing "
          "--disable-translate "
          "--disable-extensions "
          "--disable-component-update "
          "--disable-default-apps");
#else
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
          "--disable-gpu-compositing "
          "--disable-translate "
          "--disable-extensions "
          "--disable-component-update "
          "--disable-default-apps");
#endif
#else
#ifdef QT_DEBUG
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
    "--remote-debugging-port=9421 "
          "--disable-gpu "
          "--disable-gpu-compositing "
          "--disable-translate "
          "--disable-extensions "
          "--disable-component-update "
          "--disable-default-apps "
          "--no-sandbox");
#else
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
          "--disable-gpu "
          "--disable-gpu-compositing "
          "--disable-translate "
          "--disable-extensions "
          "--disable-component-update "
          "--disable-default-apps "
          "--no-sandbox");
#endif
#endif
}

int main(int argc, char *argv[]) {
  // Qt6 on Linux routes qDebug/qWarning to journald when the process is not
  // attached to a TTY (e.g. when launched from an IDE).  Force stderr output
  // so the IDE Run console always captures debug logs.
#ifdef QT_DEBUG
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif

  setChromiumFlags();

  SingleApplication instance(argc, argv, true);
  instance.setQuitOnLastWindowClosed(false);
  instance.setWindowIcon(themeIcon("whatsie", ":/icons/app/icon-64.png"));
  QApplication::setApplicationName("WhatSie");
  QApplication::setDesktopFileName("net.shakaran.whatsie");
  QApplication::setOrganizationDomain("net.shakaran");
  QApplication::setOrganizationName("shakaran");
  QApplication::setApplicationVersion(VERSIONSTR);

  installTranslations();

  // This fork changed the organisation name, which moves every QStandardPaths
  // location (settings, and the WebEngine profile that holds the WhatsApp
  // session). Carry the old data over on first run so users are not silently
  // logged out.
  migrateLegacyUserData();
  clearSilentlyDeniedPermissions();


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
          QApplication::applicationName());

  QCommandLineOption lockAppOption(QStringList() << "l"
                                                 << "lock-app",
                                   QObject::tr("Locks a running instance of ") +
                                       QApplication::applicationName());

  QCommandLineOption openAboutOption(
      QStringList() << "i"
                    << "open-about",
      QObject::tr("Opens About dialog in a running instance of ") +
          QApplication::applicationName());

  QCommandLineOption toggleThemeOption(
      QStringList() << "t"
                    << "toggle-theme",
      QObject::tr(
          "Toggle between dark & light theme in a running instance of ") +
          QApplication::applicationName());

  QCommandLineOption reloadAppOption(
      QStringList() << "r"
                    << "reload-app",
      QObject::tr("Reload the app in a running instance of ") +
          QApplication::applicationName());

  QCommandLineOption newChatOption(
      QStringList() << "n"
                    << "new-chat",
      QObject::tr("Open new chat prompt in a running instance of ") +
          QApplication::applicationName());

  QCommandLineOption buildInfoOption(QStringList() << "b"
                                                   << "build-info",
                                     "Shows detailed current build infomation");

  QCommandLineOption showAppWindowOption(
      QStringList() << "w"
                    << "show-window",
      QObject::tr("Show main window of running instance of ") +
          QApplication::applicationName());

  parser.addOption(showCLIHelpOption);
  parser.addVersionOption();
  parser.addOption(buildInfoOption);
  parser.addOption(showAppWindowOption);
  parser.addOption(openSettingsOption);
  parser.addOption(lockAppOption);
  parser.addOption(openAboutOption);
  parser.addOption(toggleThemeOption);
  parser.addOption(reloadAppOption);
  parser.addOption(newChatOption);

  secondaryInstanceCLIOptions << showAppWindowOption << openSettingsOption
                              << lockAppOption << openAboutOption
                              << toggleThemeOption << reloadAppOption
                              << newChatOption;

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
    qInfo().noquote() << QApplication::applicationName() +
                             " is already running with PID: " +
                             QString::number(instance.primaryPid()) +
                             " by USER:"
                      << instance.primaryUser();
    return 0;
  }

  // Initialise the single persistent WebEngine profile before any page is created.
  WebEngineProfileManager::instance();

  MainWindow whatsie;

  // else
  QObject::connect(
      &instance, &SingleApplication::receivedMessage, &whatsie,
      [&whatsie, &secondaryInstanceCLIOptions](int instanceId,
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
          whatsie.alreadyRunning();
          whatsie.showSettings(true);
          return;
        }

        if (p.isSet("l")) {
          qInfo() << "cmd:"
                  << "LockApp";
          whatsie.alreadyRunning();
          if (!SettingsManager::instance()
                   .settings()
                   .value("asdfg")
                   .isValid()) {
            whatsie.showNotification(
                QApplication::applicationName(),
                QObject::tr("App lock is not configured, \n"
                            "Please setup the password in the Settings "
                            "first."));
          } else {
            whatsie.lockApp();
          }
          return;
        }

        if (p.isSet("i")) {
          qInfo() << "cmd:"
                  << "OpenAppAbout";
          whatsie.alreadyRunning();
          whatsie.showAbout();
          return;
        }

        if (p.isSet("t")) {
          qInfo() << "cmd:"
                  << "ToggleAppTheme";
          whatsie.alreadyRunning();
          whatsie.toggleTheme();
          return;
        }

        if (p.isSet("r")) {
          qInfo() << "cmd:"
                  << "ReloadApp";
          whatsie.alreadyRunning();
          whatsie.doReload(false, true);
          return;
        }

        if (p.isSet("n")) {
          qInfo() << "cmd:"
                  << "OpenNewChatPrompt";
          whatsie.alreadyRunning();
          whatsie.newChat(); // TODO: invetigate the crash
          return;
        }

        if (p.isSet("w")) {
          qInfo() << "cmd:"
                  << "ShowAppWindow";
          whatsie.alreadyRunning();
          whatsie.show();
          return;
        }

        if (messageStr.contains("whatsapp://", Qt::CaseInsensitive)) {
          QString urlStr =
              "whatsapp://" + messageStr.split("whatsapp://").last();
          qInfo() << "cmd:"
                  << "x-schema-handler";
          whatsie.loadSchemaUrl(urlStr);
        } else {
          whatsie.alreadyRunning();
        }
      });

  foreach (QString argStr, instance.arguments()) {
    if (argStr.contains("whatsapp://")) {
      qInfo() << "cmd:"
              << "x-schema-handler";
      whatsie.loadSchemaUrl(argStr);
    }
  }

  if (QSystemTrayIcon::isSystemTrayAvailable() &&
      SettingsManager::instance()
          .settings()
          .value("startMinimized", false)
          .toBool()) {
    whatsie.runMinimized();
  } else {
    whatsie.show();
  }

  return instance.exec();
}
