#include "updatechecker.h"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#ifndef VERSIONSTR
#define VERSIONSTR "0.0.0"
#endif

namespace {
const char kReleasesUrl[] =
    "https://api.github.com/repos/shakaran/whatly/releases/latest";
const char kEnabledKey[] = "checkForUpdates";
const char kLastCheckKey[] = "update/lastCheckMs";
constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

// The releases endpoint, overridable through WHATLY_UPDATE_URL so the check path
// can be pointed at a local stand-in in tests; in normal use it is GitHub's.
QString releasesUrl() {
  const QString env = qEnvironmentVariable("WHATLY_UPDATE_URL");
  return env.isEmpty() ? QString::fromLatin1(kReleasesUrl) : env;
}
} // namespace

namespace UpdateCheck {

int compareVersions(const QString &a, const QString &b) {
  auto parts = [](QString v) {
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
      v.remove(0, 1);
    QList<int> out;
    const auto segs = v.split(QLatin1Char('.'));
    for (const QString &s : segs) {
      // Stop a segment at the first non-digit ("1-rc2" → 1).
      int n = 0;
      bool any = false;
      for (const QChar &c : s) {
        if (!c.isDigit())
          break;
        n = n * 10 + (c.digitValue());
        any = true;
      }
      out << (any ? n : 0);
    }
    return out;
  };
  const QList<int> pa = parts(a), pb = parts(b);
  const int n = qMax(pa.size(), pb.size());
  for (int i = 0; i < n; ++i) {
    const int x = i < pa.size() ? pa[i] : 0;
    const int y = i < pb.size() ? pb[i] : 0;
    if (x != y)
      return x < y ? -1 : 1;
  }
  return 0;
}

QString latestFromJson(const QByteArray &json, QString *url) {
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  const QString tag = o.value(QStringLiteral("tag_name")).toString();
  if (url)
    *url = o.value(QStringLiteral("html_url")).toString();
  return tag;
}

Install installFrom(bool flatpakInfoExists, const QString &appImagePath,
                    const QString &executablePath) {
  // Order matters. Inside a Flatpak everything lives under /app, and the sandbox
  // sets neither APPIMAGE nor a /usr path that means what it appears to mean, so
  // it has to be recognised before the others.
  if (flatpakInfoExists)
    return Install::Flatpak;
  if (!appImagePath.isEmpty())
    return Install::AppImage;
  // /usr is what actually marks a package the distribution will update itself.
  // Our own .deb and .rpm unpack to /opt/whatly precisely because they bundle
  // their Qt, and their user does fetch the next build by hand — so they belong
  // with Unknown, not here.
  if (executablePath.startsWith(QLatin1String("/usr/")))
    return Install::DistroPackage;
  return Install::Unknown;
}

Install currentInstall() {
  const bool flatpak = QFileInfo::exists(QStringLiteral("/.flatpak-info")) ||
                       !qEnvironmentVariableIsEmpty("FLATPAK_ID");
  return installFrom(flatpak, qEnvironmentVariable("APPIMAGE"),
                     QCoreApplication::applicationFilePath());
}

} // namespace UpdateCheck

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

bool UpdateChecker::isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEnabledKey), true)
      .toBool();
}

void UpdateChecker::setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEnabledKey),
                                                  enabled);
}

void UpdateChecker::check(bool force) {
  if (!force) {
    if (!isEnabled())
      return;
    const qint64 last = SettingsManager::instance()
                            .settings()
                            .value(QLatin1String(kLastCheckKey), 0)
                            .toLongLong();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last != 0 && now - last < kDayMs)
      return; // checked recently
  }

  if (!m_net)
    m_net = new QNetworkAccessManager(this);

  QNetworkRequest req{QUrl(releasesUrl())};
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Whatly"));
  QNetworkReply *reply = m_net->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    SettingsManager::instance().settings().setValue(
        QLatin1String(kLastCheckKey), QDateTime::currentMSecsSinceEpoch());
    if (reply->error() != QNetworkReply::NoError) {
      emit checkFailed(reply->errorString());
      return;
    }
    QString url;
    const QString latest = UpdateCheck::latestFromJson(reply->readAll(), &url);
    if (latest.isEmpty()) {
      emit checkFailed(tr("Could not read the latest release"));
      return;
    }
    if (UpdateCheck::compareVersions(latest,
                                     QStringLiteral(VERSIONSTR)) > 0)
      emit updateAvailable(latest, url);
    else
      emit upToDate();
  });
}
