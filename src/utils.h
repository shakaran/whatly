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

class QLabel;

class Utils : public QObject {
  Q_OBJECT

public:
  Utils(QObject *parent = 0);
  virtual ~Utils();
  static QString getInstallType();
  // The version, with the build label appended when a build sets one. Under the
  // Settings window's title, small and faint, where the app's own icon and name
  // are already saying whose version it is — so which build is running can be
  // read off the app instead of dug out of About, the question every test round
  // starts with. Nowhere in a window anyone is reading messages in: nearly
  // nobody wants this, and there it would be in front of them always.
  static QString versionLabel();
  // The application's display name followed by that. The long form, for the
  // tooltip the account strip answers a settled hover with — a tip floats free
  // of anything that would say which application it belongs to.
  static QString appNameWithVersion();
  // True when a JS console message is WhatsApp Web's own module loader reporting
  // unresolved dependencies (the "… with unresolved dependencies … cr:NNNN is
  // not defined" cascade). That means the web app never finished initialising —
  // usually a corrupt/locked profile or a partial cached bundle, not a login
  // problem. Matches conservatively (both markers) so ordinary page errors do
  // not trip it. See issue #43.
  static bool isWhatsAppLoadFailure(const QString &consoleMessage);
  // True when a JS console message is well-understood, harmless noise that only
  // drowns the lines a bug report needs: WhatsApp's own de-identified-telemetry
  // fetch being blocked by CORS (its endpoint sends no ACAO header — the same in
  // a plain browser), the Permissions-Policy header naming features this Chromium
  // build does not implement (bluetooth, otp-credentials, payment, usb), and
  // reason-less promise rejections ("Uncaught (in promise) undefined") that
  // WhatsApp Web floods when its session state is unhealthy. All repeat by the
  // dozen and none means anything actionable. Matched conservatively so real
  // CORS/policy errors — and promise rejections that carry a value — still show.
  static bool isBenignWebConsoleNoise(const QString &consoleMessage);
  // True when a JS console message is the Service Worker failing to register
  // because its on-disk CacheStorage is corrupt ("wrong file structure on disk"
  // surfaces to the page as a registration failure). Chromium self-heals a
  // corrupt IndexedDB but not this cache, so the failure sticks across reloads
  // and helps stall WhatsApp Web's bootstrap (see isWhatsAppLoadFailure). The
  // page can clear it with its own caches/serviceWorker APIs. See issue #43.
  static bool isServiceWorkerRegistrationFailure(const QString &consoleMessage);
  // Whether to force the XCB (XWayland) platform instead of native Wayland.
  // Proprietary NVIDIA on a Wayland session frequently ships no working
  // wayland-egl, so Qt cannot get a QRhi for the widget backing store and the
  // window comes up blank and unfocusable — with no way to reach Settings to
  // fix it (issue #84). XCB has GLX and works there. True only when all three
  // hold: the user has not chosen a platform themselves (their choice always
  // wins), the session is Wayland, and the proprietary NVIDIA driver is present.
  // Pure so the policy is unit-tested; main() gathers the three facts from the
  // environment. NVIDIA-on-Wayland users who do have it working can still set
  // QT_QPA_PLATFORM=wayland to override.
  static bool shouldPreferXcbPlatform(bool userChosePlatform,
                                      bool waylandSession, bool nvidiaProprietary);
  // The path to AppImageUpdate's CLI (appimageupdatetool, or the older
  // AppImageUpdate) on PATH, or "" when neither is installed. The AppImage
  // carries its own zsync update information, so this tool fetches only the
  // changed blocks instead of the whole ~150 MB image. See issue #85.
  static QString appImageUpdateTool();
  // Whether Whatly can offer to update itself in place: it is running as an
  // AppImage, the update tool is present, and the running image's path is known
  // (the APPIMAGE env var). Pure so the policy is unit-tested; the caller
  // supplies the three facts. Every other install kind (Flatpak, distro package)
  // is updated from outside the app and must never see this. See issue #85.
  static bool canOfferAppImageSelfUpdate(bool isAppImage, const QString &toolPath,
                                         const QString &appImagePath);
  // Small and faint: a label that has to sit beside something with a job to do
  // without competing with it. Used for both of the above.
  static void makeWatermark(QLabel *label);
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

  // Whether the window should count as "frontmost" for the tray-click hide
  // heuristic: active now, or — within a positive grace window — deactivated
  // less than graceMs ago. The grace recovers the Windows case where the tray
  // click hands focus to the shell before iconActivated() runs; graceMs <= 0
  // disables it (every other platform, where isActiveWindow() is reliable).
  // Pure, so it is unit-tested (#8).
  static bool wasFrontmostRecently(bool active, qint64 lastDeactivationMs,
                                   qint64 nowMs, int graceMs);

  // Put `all` in most-recently-used order, given a `history` that is also
  // most-recent-first and may repeat itself, name things no longer in `all`, and
  // miss things that have never been used. Anything the history does not mention
  // goes on the end in `all`'s own order — being unmentioned must not mean being
  // left out, since this order is what offers the windows to the user, and a
  // window nothing offers is a window with no way back to it. Pure and a
  // template, so it is unit-tested without a window in sight.
  template <typename T>
  static QList<T> orderedByHistory(const QList<T> &history,
                                   const QList<T> &all) {
    QList<T> out;
    for (const T &item : history)
      if (all.contains(item) && !out.contains(item))
        out << item;
    for (const T &item : all)
      if (!out.contains(item))
        out << item;
    return out;
  }
};

#endif // UTILS_H
