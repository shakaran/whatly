#ifndef UTILS_H
#define UTILS_H

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUuid>

class Utils : public QObject {
  Q_OBJECT

public:
  Utils(QObject *parent = 0);
  virtual ~Utils();
  static QString getInstallType();
  static QString refreshCacheSize(const QString cache_dir);
  static bool delete_cache(const QString cache_dir);
  static QString toCamelCase(const QString &s);
  static QString generateRandomId(int length);
  static QString convertSectoDay(qint64 secs);
  static QString returnPath(QString pathname, QString standardLocation);
  static QString encodeXML(const QString &encodeMe);
  static QString decodeXML(const QString &decodeMe);
  static QString GetEnvironmentVar(const QString &variable_name);
  static float RoundToOneDecimal(float number);
  static void DisplayExceptionErrorDialog(const QString &error_info);
  static QString appDebugInfo();

  // The same facts as appDebugInfo(), plus how much memory the browser and
  // renderer processes are actually using and the recent log, as Markdown —
  // ready to be the body of a bug report rather than something to retype.
  static QString appDebugInfoMarkdown();

  // Resident memory of this process and of the Chromium processes it spawned.
  // Memory complaints are the most common bug report this app gets, and they
  // arrive with a screenshot of a system monitor instead of numbers.
  static QString processMemoryInfo();
  static void desktopOpenUrl(const QString &filePathStr);
  static bool isPhoneNumber(const QString &phoneNumber);
  static QString genRand(int length, bool useUpper = true, bool useLower = true,
                         bool useDigits = true);
  static QString detectDesktopEnvironment();
private slots:
  // use refreshCacheSize
  static quint64 dir_size(const QString &directory);

public:
  // Top-right anchor for a popup of `size` inside the available screen rect
  // `avail`, keeping it fully on-screen with `margin` px of clearance. Uses
  // avail's own origin, so it is correct on a secondary monitor whose geometry
  // does not start at (0,0). Pure, so it is unit-tested (#5).
  static QPoint topRightWithin(const QRect &avail, const QSize &size,
                               int margin);
};

#endif // UTILS_H
