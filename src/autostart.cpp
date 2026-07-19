#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace {
#if defined(Q_OS_WIN)
QString runKey() {
  return QStringLiteral(
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
const char kValueName[] = "Whatly";
#elif defined(Q_OS_LINUX)
// The XDG autostart entry lives next to the user's other autostart .desktop
// files. Named after the app id so it is easy to spot and never collides.
QString autostartFilePath() {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
      QStringLiteral("/autostart");
  return dir + QStringLiteral("/net.shakaran.whatly.desktop");
}
#endif
} // namespace

namespace Autostart {

bool isSupported() {
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
  return true;
#else
  return false;
#endif
}

bool isEnabled() {
#if defined(Q_OS_WIN)
  QSettings reg(runKey(), QSettings::NativeFormat);
  return !reg.value(QLatin1String(kValueName)).toString().isEmpty();
#elif defined(Q_OS_LINUX)
  return QFile::exists(autostartFilePath());
#else
  return false;
#endif
}

bool setEnabled(bool enabled) {
#if defined(Q_OS_WIN)
  QSettings reg(runKey(), QSettings::NativeFormat);
  if (enabled) {
    // Quote the path so a space in it does not split the command.
    const QString cmd =
        QLatin1Char('"') +
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
        QLatin1Char('"');
    reg.setValue(QLatin1String(kValueName), cmd);
  } else {
    reg.remove(QLatin1String(kValueName));
  }
  reg.sync();
  return reg.status() == QSettings::NoError;
#elif defined(Q_OS_LINUX)
  const QString path = autostartFilePath();
  if (!enabled) {
    if (!QFile::exists(path))
      return true;
    return QFile::remove(path);
  }
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  QTextStream out(&f);
  out << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=Whatly\n"
      << "Icon=net.shakaran.whatly\n"
      << "Exec=" << QCoreApplication::applicationFilePath() << "\n"
      << "Terminal=false\n"
      << "X-GNOME-Autostart-enabled=true\n";
  f.close();
  return true;
#else
  Q_UNUSED(enabled);
  return false;
#endif
}

} // namespace Autostart
