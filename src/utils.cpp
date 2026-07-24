#include "utils.h"
#include "debuglog.h"
#include "def.h"
#include <QStandardPaths>
#include <QHash>
#include <time.h>

Utils::Utils(QObject *parent) : QObject(parent) { setParent(parent); }

Utils::~Utils() { this->deleteLater(); }

// calculate dir size
quint64 Utils::dir_size(const QString &directory) {
  quint64 sizex = 0;
  QFileInfo str_info(directory);
  if (str_info.isDir()) {
    QDir dir(directory);
    QFileInfoList list =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden |
                          QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (int i = 0; i < list.size(); ++i) {
      QFileInfo fileInfo = list.at(i);
      if (fileInfo.isDir()) {
        sizex += dir_size(fileInfo.absoluteFilePath());
      } else {
        sizex += fileInfo.size();
      }
    }
  }
  return sizex;
}

// get the size of cache folder in human readble format
QString Utils::refreshCacheSize(const QString cache_dir) {
  qint64 cache_size = dir_size(cache_dir);
  QString cache_unit;
  if (cache_size > 1024 * 1024 * 1024) {
    cache_size = cache_size / (1024 * 1024 * 1024);
    cache_unit = " GB";
  }
  if (cache_size > 1024 * 1024) {
    cache_size = cache_size / (1024 * 1024);
    cache_unit = " MB";
  } else if (cache_size > 1024) {
    cache_size = cache_size / (1024);
    cache_unit = " kB";
  } else {
    cache_unit = " B";
  }
  return QString::number(cache_size) + cache_unit;
}

// True only for a path that is safe to recursively delete: it must be a real,
// absolute directory sitting *inside* one of the app's own storage roots.
//
// This exists because an empty path here wiped people's home directories.
// QDir("").removeRecursively() operates on ".", the current working directory,
// which for an app launched from a desktop or file manager is the user's home —
// so if the profile ever handed back an empty cache/storage path (an
// off-the-record profile does exactly that), clearing the cache deleted the
// home directory. Proven with a throwaway QDir("").removeRecursively() that
// emptied its cwd. Never trust the path; verify it.
static bool isSafeToDelete(const QString &path) {
  if (path.trimmed().isEmpty())
    return false;

  const QString clean = QDir(path).absolutePath();
  const QFileInfo info(clean);
  if (!info.isAbsolute() || clean == QLatin1String("/") ||
      clean == QDir::homePath() || clean == QDir::rootPath())
    return false;

  // Must live under a location the app actually owns. Anything else — the home
  // directory, the working directory, a system path — is refused outright.
  const QList<QStandardPaths::StandardLocation> roots = {
      QStandardPaths::AppLocalDataLocation, QStandardPaths::AppDataLocation,
      QStandardPaths::CacheLocation,        QStandardPaths::AppConfigLocation,
      QStandardPaths::GenericDataLocation,  QStandardPaths::GenericCacheLocation,
  };
  for (const auto root : roots) {
    for (const QString &base : QStandardPaths::standardLocations(root)) {
      if (base.isEmpty())
        continue;
      const QString baseClean = QDir(base).absolutePath();
      if (clean == baseClean ||
          clean.startsWith(baseClean + QLatin1Char('/')))
        return true;
    }
  }
  return false;
}

bool Utils::delete_cache(const QString cache_dir) {
  if (!isSafeToDelete(cache_dir)) {
    qWarning() << "Refusing to delete an unsafe cache path:" << cache_dir
               << "(this guard exists because an empty path once wiped home "
                  "directories — see issue #230)";
    return false;
  }
  bool deleted = QDir(cache_dir).removeRecursively();
  QDir(cache_dir).mkpath(cache_dir);
  return deleted;
}

// returns string with first letter capitalized
QString Utils::toCamelCase(const QString &s) {
  QStringList parts = s.split(' ', Qt::SkipEmptyParts);
  for (int i = 0; i < parts.size(); ++i)
    parts[i].replace(0, 1, parts[i][0].toUpper());
  return parts.join(" ");
}

QString Utils::generateRandomId(int length) {
  return genRand(length, false, true, false);
}

QString Utils::genRand(int length, bool useUpper, bool useLower,
                       bool useDigits) {
  QString possibleCharacters;
  if (useUpper) {
    possibleCharacters += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  }
  if (useLower) {
    possibleCharacters += "abcdefghijklmnopqrstuvwxyz";
  }
  if (useDigits) {
    possibleCharacters += "0123456789";
  }

  if (possibleCharacters.isEmpty()) {
    return genRand(length, true, true, true);
  }

  QByteArray randomBytes;
  randomBytes.resize(length);
  for (int i = 0; i < length; ++i) {
    randomBytes[i] =
        possibleCharacters[QRandomGenerator::securelySeeded().generate() %
                           possibleCharacters.length()]
            .toLatin1();
  }
  return QString::fromLatin1(randomBytes);
}

QString Utils::convertSectoDay(qint64 secs) {
  int day = secs / (24 * 3600);

  secs = secs % (24 * 3600);
  int hour = secs / 3600;

  secs %= 3600;
  int minutes = secs / 60;

  secs %= 60;
  int seconds = secs;

  QString days = QString::number(day) + " " + "days " + QString::number(hour) +
                 " " + "hours " + QString::number(minutes) + " " + "minutes " +
                 QString::number(seconds) + " " + "seconds ";
  return days;
}

/**
 * @brief Returns the full path for a given file or directory name within a
 * specified location.
 *
 * The method constructs the full path by concatenating the specified `pathname`
 * with the provided `standardLocation`. If `standardLocation` is not provided,
 * the default value is obtained from
 * `QStandardPaths::writableLocation(QStandardPaths::DataLocation)`.
 *
 * @param pathname The name of the file or directory within the specified
 * location.
 * @param standardLocation (optional) The base directory to prepend to
 * `pathname`. Default value:
 * QStandardPaths::writableLocation(QStandardPaths::DataLocation).
 *
 * @return The full path, including the `pathname` and the `standardLocation`,
 * separated by the appropriate directory separator. If the specified location
 * does not exist, the method creates it using QDir::mkpath. The returned path
 * ends with a directory separator.
 */
QString
Utils::returnPath(QString pathname,
                  QString standardLocation = QStandardPaths::writableLocation(
                      QStandardPaths::AppLocalDataLocation)) {
  QChar sepe = QDir::separator();
  QDir d(standardLocation + sepe + pathname);
  d.mkpath(standardLocation + sepe + pathname);
  return standardLocation + sepe + pathname + sepe;
}

/**
 * @brief Encodes XML special characters in a given string and returns the
 * encoded version.
 *
 * The method iterates over each character in the input `encodeMe` string and
 * replaces XML special characters
 * ('&', '\'', '"', '<', and '>') with their corresponding XML entities
 * ('&amp;', '&apos;', '&quot;', '&lt;', and '&gt;'). The encoded string is then
 * returned.
 *
 * @param encodeMe The string to be encoded.
 * @return The encoded version of the input string, where XML special characters
 * are replaced with their corresponding entities.
 */
QString Utils::encodeXML(const QString &encodeMe) {
  QString temp;
  int length = encodeMe.size();

  for (int index = 0; index < length; index++) {
    QChar character(encodeMe.at(index));

    switch (character.unicode()) {
    case '&':
      temp += "&amp;";
      break;

    case '\'':
      temp += "&apos;";
      break;

    case '"':
      temp += "&quot;";
      break;

    case '<':
      temp += "&lt;";
      break;

    case '>':
      temp += "&gt;";
      break;

    default:
      temp += character;
      break;
    }
  }

  return temp;
}

/**
 * @brief Decodes XML entities in a given string and returns the decoded
 * version.
 *
 * The method creates a copy of the input `decodeMe` string and replaces XML
 * entities
 * ('&amp;', '&apos;', '&quot;', '&lt;', and '&gt;') with their corresponding
 * characters. The decoded string is then returned.
 *
 * @param decodeMe The string to be decoded.
 * @return The decoded version of the input string, where XML entities are
 * replaced with their corresponding characters.
 */
QString Utils::decodeXML(const QString &decodeMe) {
  QString temp(decodeMe);

  temp.replace("&amp;", "&");
  temp.replace("&apos;", "'");
  temp.replace("&quot;", "\"");
  temp.replace("&lt;", "<");
  temp.replace("&gt;", ">");

  return temp;
}

/**
 * @brief Retrieves the installation type based on the environment variables and
 * heuristics.
 *
 * The method retrieves the value of the "INSTALL_TYPE" environment variable
 * using QProcessEnvironment. If the "INSTALL_TYPE" variable is empty, the
 * method performs heuristics to determine the installation type. It checks if
 * the "SNAP" environment variable is set, in which case the installation type
 * is considered as "snap". If the "SNAP" variable is not set, it checks if the
 * "FLATPAK_ID" environment variable is not empty, in which case the
 * installation type is considered as "flatpak".
 *
 * @return The installation type as a QString. It can be "snap", "flatpak", or
 * an empty string if the installation type could not be determined.
 */
QString Utils::getInstallType() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Retrieve the value of "INSTALL_TYPE" environment variable
  QString installType = env.value("INSTALL_TYPE");

  // Try guessing if it is empty
  if (installType.isEmpty()) {
    if (env.contains("SNAP")) {
      installType = "snap";
    } else if (env.value("FLATPAK_ID").trimmed().isEmpty() == false) {
      installType = "flatpak";
    }
  }

  return installType.trimmed();
}

/**
 * @brief Retrieves debug information about the application.
 *
 * The method generates a formatted string containing various debug information
 * about the application. It includes information such as the application name,
 * version, source branch, commit hash, build datetime, Qt runtime version, Qt
 * compiled version, system information, and installation mode.
 *
 * @return A QString containing the debug information about the application.
 */
QString Utils::appDebugInfo() {
  QString installType = Utils::getInstallType();
  installType =
      "<li><b>" + QObject::tr("Install mode") + ":</b>        " +
      QString(installType.trimmed().isEmpty() ? "Unknown" : installType) +
      "</li>";

  QString applicationName = QApplication::applicationDisplayName();
  QString version = QString(VERSIONSTR);
  QString sourceBranch = QString(GIT_BRANCH);
  QString commitHash = QString(GIT_HASH);
  QString buildDatetime = QString::fromLatin1(BUILD_TIMESTAMP);
  QString qtRuntimeVersion = QString(qVersion());
  QString qtCompiledVersion = QString(QT_VERSION_STR);
  QString systemInfo = QSysInfo::prettyProductName();
  QString architecture = QSysInfo::currentCpuArchitecture();
  QString installMode = Utils::getInstallType();

  QStringList debugInfo;
  debugInfo << "<h3>" + applicationName + "</h3>"
            << "<ul>"
            << "<li><b>" + QObject::tr("Version") + ":</b>             " +
                   version + "</li>"
            << "<li><b>" + QObject::tr("Source Branch") + ":</b>             " +
                   sourceBranch + "</li>"
            << "<li><b>" + QObject::tr("Commit Hash") + ":</b>             " +
                   commitHash + "</li>"
            << "<li><b>" + QObject::tr("Build Datetime") + ":</b>          " +
                   buildDatetime + "</li>"
            << "<li><b>" + QObject::tr("Qt Runtime Version") + ":</b>   " +
                   qtRuntimeVersion + "</li>"
            << "<li><b>" + QObject::tr("Qt Compiled Version") + ":</b> " +
                   qtCompiledVersion + "</li>"
            << "<li><b>" + QObject::tr("System") + ":</b>              " +
                   systemInfo + "</li>"
            << "<li><b>" + QObject::tr("Architecture") + ":</b>        " +
                   architecture + "</li>"
            << "<li><b>" + QObject::tr("Install mode") + ":</b>        " +
                   QString(installMode.trimmed().isEmpty() ? "Unknown"
                                                           : installMode) +
                   "</li>"
            << "</ul>";

  return debugInfo.join("\n");
}

// Resident set size of a process, in MB, or -1 when it cannot be read.
static double residentMb(qint64 pid) {
#ifdef Q_OS_LINUX
  QFile status(QStringLiteral("/proc/%1/status").arg(pid));
  if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
    return -1;
  while (!status.atEnd()) {
    const QByteArray line = status.readLine();
    if (!line.startsWith("VmRSS:"))
      continue;
    const QList<QByteArray> parts = line.simplified().split(' ');
    if (parts.size() >= 2)
      return parts.at(1).toDouble() / 1024.0;  // kB -> MB
  }
#else
  Q_UNUSED(pid);
#endif
  return -1;
}

QString Utils::processMemoryInfo() {
#ifdef Q_OS_LINUX
  const qint64 self = QCoreApplication::applicationPid();
  const double own = residentMb(self);

  // Walk the whole subtree, not just our direct children. Chromium forks its
  // renderer from the *zygote*, so the renderer's parent is the zygote and not
  // us — and the renderer is exactly where WhatsApp Web lives and where the
  // gigabytes people report go. Counting direct children only reported half a
  // gigabyte while the process tree was using two and a half.
  QHash<qint64, QList<qint64>> childrenOf;
  const QStringList entries = QDir(QStringLiteral("/proc"))
                                  .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &entry : entries) {
    bool isPid = false;
    const qint64 pid = entry.toLongLong(&isPid);
    if (!isPid)
      continue;
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    while (!status.atEnd()) {
      const QByteArray line = status.readLine();
      if (!line.startsWith("PPid:"))
        continue;
      childrenOf[line.simplified().split(' ').value(1).toLongLong()].append(pid);
      break;
    }
  }

  double descendants = 0;
  int count = 0;
  QList<qint64> queue = childrenOf.value(self);
  while (!queue.isEmpty()) {
    const qint64 pid = queue.takeFirst();
    queue.append(childrenOf.value(pid));
    const double rss = residentMb(pid);
    if (rss < 0)
      continue;
    descendants += rss;
    ++count;
  }

  return QStringLiteral("%1 MB browser + %2 MB across %3 engine processes "
                        "(%4 MB total)")
      .arg(own, 0, 'f', 0)
      .arg(descendants, 0, 'f', 0)
      .arg(count)
      .arg(own + descendants, 0, 'f', 0);
#else
  return QStringLiteral("unavailable on this platform");
#endif
}

QString Utils::appDebugInfoMarkdown() {
  QString installMode = Utils::getInstallType().trimmed();
  if (installMode.isEmpty())
    installMode = QStringLiteral("Unknown");

  QStringList out;
  out << QStringLiteral("### Environment")
      << QStringLiteral("")
      << QStringLiteral("| | |")
      << QStringLiteral("|---|---|")
      << QStringLiteral("| Version | %1 |").arg(QLatin1String(VERSIONSTR))
      << QStringLiteral("| Commit | %1 (%2) |")
             .arg(QLatin1String(GIT_HASH), QLatin1String(GIT_BRANCH))
      << QStringLiteral("| Built | %1 |")
             .arg(QString::fromLatin1(BUILD_TIMESTAMP))
      << QStringLiteral("| Qt | %1 (built against %2) |")
             .arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR))
      << QStringLiteral("| System | %1 |").arg(QSysInfo::prettyProductName())
      << QStringLiteral("| Architecture | %1 |")
             .arg(QSysInfo::currentCpuArchitecture())
      << QStringLiteral("| Install mode | %1 |").arg(installMode);
  // Inside a snap the store sets SNAP_REVISION; it pins down exactly which
  // build a report came from, which the version alone cannot.
  if (installMode == QLatin1String("snap"))
    out << QStringLiteral("| Snap revision | %1 |")
               .arg(qEnvironmentVariable("SNAP_REVISION",
                                         QStringLiteral("unknown")));
  out << QStringLiteral("| Memory | %1 |").arg(Utils::processMemoryInfo())
      << QStringLiteral("");

  const QString log = DebugLog::recent();
  if (!log.isEmpty()) {
    out << QStringLiteral("### Log")
        << QStringLiteral("")
        << QStringLiteral("<details><summary>Recent output and page console"
                          "</summary>")
        << QStringLiteral("")
        << QStringLiteral("```")
        << log
        << QStringLiteral("```")
        << QStringLiteral("")
        << QStringLiteral("</details>")
        << QStringLiteral("");
  }
  return out.join(QLatin1Char('\n'));
}

// GCOVR_EXCL_START
// A modal error dialog that blocks on the user closing it (exec()); it cannot be
// exercised by a headless unit test, so it is excluded from coverage rather than
// faked. Its content is trivial (a formatted string builder).
void Utils::DisplayExceptionErrorDialog(const QString &error_info) {
  QMessageBox message_box(QApplication::activeWindow());
  message_box.setAttribute(Qt::WA_DeleteOnClose, true);
  message_box.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
  message_box.setModal(true);
  message_box.setIcon(QMessageBox::Critical);
  message_box.setWindowTitle(QApplication::applicationDisplayName() +
                             QObject::tr("Exception"));
  // spaces are added to the end because otherwise the dialog is too small
  message_box.setText(QApplication::applicationDisplayName() +
                      QObject::tr(" has encountered a problem."));
  message_box.setInformativeText(
      QApplication::applicationDisplayName() +
      QObject::tr(
          " may need to Restart. Please report the error to developer."));
  message_box.setStandardButtons(QMessageBox::Close);
  QStringList detailed_text;
  detailed_text << "Error info: " + error_info
                << "\nVersion: " + QString(VERSIONSTR)
                << "\nSource Branch: " + QString(GIT_BRANCH)
                << "\nCommit Hash: " + QString(GIT_HASH)
                << "\nBuild Datetime: " + QString(BUILD_TIMESTAMP)
                << "\nQt Runtime Version: " + QString(qVersion())
                << "\nQt Compiled Version: " + QString(QT_VERSION_STR)
                << "\nSystem: " + QSysInfo::prettyProductName()
                << "\nArchitecture: " + QSysInfo::currentCpuArchitecture()
                << "\nInstall type: " + Utils::getInstallType();
  message_box.setDetailedText(detailed_text.join("\n"));
  message_box.exec();
}
// GCOVR_EXCL_STOP

/**
 * Returns the same number, but rounded to one decimal place
 *
 * @brief Utils::RoundToOneDecimal
 * @param number
 * @return
 */
float Utils::RoundToOneDecimal(float number) {
  return QString::number(number, 'f', 1).toFloat();
}

/**
 * Returns a value for the environment variable name passed;
 * if the env var isn't set, it returns an empty string
 * @brief Utils::GetEnvironmentVar
 * @param variable_name
 * @return
 */
QString Utils::GetEnvironmentVar(const QString &variable_name) {
  // The only time this might fall down is on Linux when an
  // environment variable holds bytedata. Don't use this
  // utility function for retrieval if that's the case.
  return qEnvironmentVariable(variable_name.toUtf8().constData(), "").trimmed();
}

/**
 * @brief Opens the specified URL in the default desktop application.
 *
 * The method attempts to open the URL using the `xdg-open` command-line tool.
 * If the `xdg-open` process fails to start, it falls back to
 * using `QDesktopServices` to open the URL. If both methods fail, a warning
 * message is printed to the console.
 *
 * @param str The URL to be opened.
 */
void Utils::desktopOpenUrl(const QString &filePathStr) {
#ifdef Q_OS_LINUX
  qDebug() << "Trying opening file using xdg-open" << filePathStr;

  QProcess *xdg_open = new QProcess;
  QObject::connect(xdg_open,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   [=](int exitCode, QProcess::ExitStatus exitStatus) {
                     Q_UNUSED(exitCode);
                     xdg_open->close();
                     xdg_open->deleteLater();
                     if (exitStatus != QProcess::NormalExit) {
                       qWarning() << "Failed to open URL using xdg-open";

                       QUrl url = QUrl::fromLocalFile(filePathStr);
                       qDebug() << "Opening file using desktop-services"
                                << url.toString();
                       if (!QDesktopServices::openUrl(url)) {
                         qWarning()
                             << "Failed to open URL using desktop-services";
                       }
                     }
                   });

  xdg_open->start("xdg-open", {filePathStr});
#else
  // xdg-open is Linux-only; use QDesktopServices directly elsewhere.
  QUrl url = QUrl::fromLocalFile(filePathStr);
  qDebug() << "Opening file using desktop-services" << url.toString();
  if (!QDesktopServices::openUrl(url)) {
    qWarning() << "Failed to open URL using desktop-services";
  }
#endif
}

bool Utils::isPhoneNumber(const QString &phoneNumber) {
  const QString phone = "^\\+(((\\d{2}))\\s?)?((\\d{2})|(\\((\\d{2})\\))\\s?)?("
                        "\\d{3,15})(\\-(\\d{3,15}))?$";
  static QRegularExpression reg(phone);
  return reg.match(phoneNumber).hasMatch();
}

QString Utils::detectDesktopEnvironment() {
    QString desktopEnv = qgetenv("XDG_CURRENT_DESKTOP");

    if (!desktopEnv.isEmpty()) {
        return desktopEnv;
    }

    QString wm = qgetenv("WINDOWMANAGER");
    QString session = qgetenv("DESKTOP_SESSION");

    if (!wm.isEmpty()) {
        return wm;
    }

    if (!session.isEmpty()) {
        return session;
    }

    return "Unknown Desktop Environment";
}

QPoint Utils::topRightWithin(const QRect &avail, const QSize &size, int margin) {
  return QPoint(avail.x() + avail.width() - size.width() - margin,
                avail.y() + margin);
}

bool Utils::wasFrontmostRecently(bool active, qint64 lastDeactivationMs,
                                 qint64 nowMs, int graceMs) {
  if (active)
    return true;
  return graceMs > 0 && (nowMs - lastDeactivationMs) < graceMs;
}
