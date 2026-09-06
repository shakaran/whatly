#include "dictionarybootstrap.h"
#include "dictionaries.h"
#include "dictionarymanager.h"
#include "settingsmanager.h"

#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
// One attempt at startup plus a short bounded backoff for a connection still
// coming up, then nothing until the next launch. See the header for why this is
// not a polling retry.
constexpr int kMaxAttempts = 3;

// Delay before retry `attempt` (1-based into the schedule). Tiny under
// WHATLY_DICT_TEST_FAST so the give-up path is testable without an 11s wait;
// the production schedule is a short bounded backoff for a connection still
// coming up.
int backoffMs(int attempt) {
  static const bool fast = qEnvironmentVariableIsSet("WHATLY_DICT_TEST_FAST");
  static const int prod[kMaxAttempts] = {0, 3000, 8000};
  static const int test[kMaxAttempts] = {0, 10, 20};
  return (fast ? test : prod)[attempt];
}

QString optOutKey() {
  return QStringLiteral("dictionaries/systemFetchOptOut");
}
QString notInManifestKey() {
  return QStringLiteral("dictionaries/systemFetchNotInManifest");
}
QString manifestTagKey() {
  return QStringLiteral("dictionaries/systemFetchManifestTag");
}
} // namespace

DictionaryBootstrap::DictionaryBootstrap(QObject *parent) : QObject(parent) {}

bool DictionaryBootstrap::shouldAttempt() {
  // Something is already installed (bundled or downloaded): never act. A system
  // that lost its only dictionary by hand comes back here with nothing installed
  // and is recoverable, which is the right discrimination.
  if (!DictionaryManager::installed().isEmpty())
    return false;
  return !SettingsManager::instance()
              .settings()
              .value(optOutKey(), false)
              .toBool();
}

void DictionaryBootstrap::start() {
  if (m_running)
    return;
  if (!shouldAttempt()) {
    emit finished(false, QString());
    return;
  }
  beginFetch();
}

void DictionaryBootstrap::beginFetch() {
  if (m_running)
    return;
  m_running = true;
  m_attempts = 0;
  attemptManifest();
}

void DictionaryBootstrap::attemptManifest() {
  if (!m_net)
    m_net = new QNetworkAccessManager(this);
  ++m_attempts;

  QNetworkRequest req{QUrl(DictionaryManager::manifestUrl())};
  req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Whatly"));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy); // release 302
  QNetworkReply *reply = m_net->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      // Network not up yet, or the fetch is blocked. Retry within the session a
      // bounded number of times, then give up until the next launch.
      if (m_attempts < kMaxAttempts) {
        QTimer::singleShot(backoffMs(m_attempts), this,
                           &DictionaryBootstrap::attemptManifest);
        return;
      }
      qInfo().noquote() << "[dictionaries] first-run fetch gave up (no network "
                           "within this session):"
                        << reply->errorString();
      m_running = false;
      emit finished(false, QString());
      return;
    }
    onManifestBody(reply->readAll());
  });
}

void DictionaryBootstrap::onManifestBody(const QByteArray &body) {
  const QList<DictionaryEntry> entries = DictionaryManager::parseManifest(body);
  QStringList codes;
  for (const DictionaryEntry &e : entries)
    codes << e.code;

  const QString locale = QLocale::system().name();
  const QString candidate = Dictionaries::systemFetchCandidate(codes, locale);

  if (candidate.isEmpty()) {
    recordNotInManifest(codes, locale);
    m_running = false;
    emit finished(false, QString());
    return;
  }

  // Found it. A system may return here after a hand-deletion, so clear any stale
  // "not in manifest" note now that a real fetch is going ahead.
  auto &s = SettingsManager::instance().settings();
  s.remove(notInManifestKey());
  s.remove(manifestTagKey());

  DictionaryEntry entry;
  for (const DictionaryEntry &e : entries)
    if (e.code == candidate) {
      entry = e;
      break;
    }

  if (!m_dict)
    m_dict = new DictionaryManager(this);
  connect(m_dict, &DictionaryManager::downloadFinished, this,
          [this](const QString &code, bool ok, const QString &error) {
            if (ok)
              qInfo().noquote()
                  << "[dictionaries] first-run fetch installed" << code
                  << "(active on the next launch)";
            else
              qWarning().noquote()
                  << "[dictionaries] first-run fetch of" << code
                  << "failed:" << error;
            m_running = false;
            emit finished(ok, ok ? code : QString());
          });
  m_dict->download(entry);
}

void DictionaryBootstrap::recordNotInManifest(const QStringList &codes,
                                              const QString &locale) {
  const QString tag = Dictionaries::manifestTag(codes);
  auto &s = SettingsManager::instance().settings();
  const bool alreadyNoted = s.value(notInManifestKey(), false).toBool() &&
                            s.value(manifestTagKey()).toString() == tag;
  if (!alreadyNoted)
    // Log once per manifest. A language the dictionary release does not carry
    // (an "eo" system hits this today) leaves spell-check off; saying so once
    // turns "why did nothing happen" into something answerable. It is not
    // repeated every launch, only when the manifest itself changes.
    qInfo().noquote() << "[dictionaries] no dictionary for the system language"
                      << locale
                      << "in the manifest; spell-check stays off until one is "
                         "added upstream or chosen in Settings";
  s.setValue(notInManifestKey(), true);
  s.setValue(manifestTagKey(), tag);
}
