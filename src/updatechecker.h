#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Checks GitHub Releases for a newer Whatly and, if found, tells the user (never
// downloads or installs anything on its own). Throttled to at most once a day
// and fully opt-out. The version comparison and the release-JSON parsing are
// pure static functions, so they are unit-tested without any network.
namespace UpdateCheck {
// Compare dotted numeric versions (a leading "v" is ignored). Returns <0 if a<b,
// 0 if equal, >0 if a>b. Missing components count as 0 ("6.3" == "6.3.0").
int compareVersions(const QString &a, const QString &b);

// Extract the latest release tag from the GitHub releases-latest JSON, and its
// html_url via *url. Returns an empty string if the JSON has no tag.
QString latestFromJson(const QByteArray &json, QString *url);

// How this copy of Whatly was installed. It decides whether "go and fetch the
// new build" is useful advice or actively wrong: with a Flatpak or a distribution
// package the package manager owns the update, and a sandboxed application
// cannot replace itself in any case.
enum class Install {
  Unknown,       // Windows, macOS, a portable archive: the user fetches the build
  DistroPackage, // under /usr, so AUR / OBS / Gentoo / a distro repo owns it
  Flatpak,
  AppImage, // carries its own update information, so it can patch in place
};

// The decision as a pure function of the three facts that separate the cases, so
// the whole table is unit-testable without touching the real environment.
Install installFrom(bool flatpakInfoExists, const QString &appImagePath,
                    const QString &executablePath);

// The same decision, taken from the environment this process is running in.
Install currentInstall();
} // namespace UpdateCheck

class UpdateChecker : public QObject {
  Q_OBJECT
public:
  explicit UpdateChecker(QObject *parent = nullptr);

  // Whether the automatic once-a-day check is enabled (default true).
  static bool isEnabled();
  static void setEnabled(bool enabled);

  // Query GitHub. Unless `force`, it is a no-op when disabled or checked within
  // the last 24h. Emits updateAvailable() / upToDate() / checkFailed().
  void check(bool force = false);

signals:
  void updateAvailable(const QString &version, const QString &url);
  void upToDate();
  void checkFailed(const QString &error);

private:
  QNetworkAccessManager *m_net = nullptr;
};

#endif // UPDATECHECKER_H
