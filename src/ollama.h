#ifndef OLLAMA_H
#define OLLAMA_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

// Helper for the local Ollama runner (idea #5 ease-of-use): detect it, list the
// installed models, and download a recommended light one with progress, so a
// non-technical user does not have to touch a terminal. Pure helpers (URL
// derivation, tag/progress parsing, the recommended list) are unit-tested; the
// HTTP lives in OllamaManager.
namespace Ollama {

// A recommended, light, instruction-following model that runs well on modest
// hardware (small RAM/CPU footprint).
struct RecModel {
  QString name;   // e.g. "qwen2.5:3b"
  QString size;   // human approx, e.g. "~1.9 GB"
  QString note;   // one-line description
};

// The curated list of light models we suggest downloading.
QList<RecModel> recommendedModels();

// True when the endpoint points at a local host (localhost/127.0.0.1/::1),
// i.e. very likely an Ollama or LM Studio on this machine.
bool isLocalEndpoint(const QString &endpoint);

// Derive the API base ("http://host:port") from a chat-completions endpoint, so
// /api/tags and /api/pull can be reached. Falls back to the default Ollama base
// when the endpoint is empty or not parseable.
QString baseUrl(const QString &endpoint);

// Installed model names from an /api/tags JSON body.
QStringList parseInstalledModels(const QByteArray &tagsJson);

// Percentage (0-100, or -1 if unknown) from one /api/pull streamed JSON line;
// *status receives the human status string ("pulling…", "verifying…", etc.).
int parsePullProgress(const QByteArray &jsonLine, QString *status);

} // namespace Ollama

class OllamaManager : public QObject {
  Q_OBJECT
public:
  explicit OllamaManager(QObject *parent = nullptr);

  // GET <base>/api/tags. Emits checked() with availability and installed models.
  void check(const QString &base);
  // POST <base>/api/pull to download `model`, streaming progress. Emits
  // pullProgress()/pullFinished().
  void pull(const QString &base, const QString &model);

signals:
  void checked(bool available, const QStringList &installedModels);
  void pullProgress(int percent, const QString &status);
  void pullFinished(bool ok, const QString &error);

private:
  QNetworkAccessManager *m_net = nullptr;
};

#endif // OLLAMA_H
