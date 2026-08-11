#ifndef AIASSISTANT_H
#define AIASSISTANT_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// AI assistant (idea #5) over an OpenAI-compatible /chat/completions endpoint.
// One protocol reaches OpenAI, OpenRouter, Groq and local runners (Ollama,
// LM Studio), so it can be kept fully local. The HTTP request is made from C++,
// never the page, so the endpoint and key never touch WhatsApp Web. Request
// building, response parsing, the prompts and the context script are pure and
// unit-tested; only send() needs the network.
namespace Ai {

bool isEnabled();   // default false (opt-in)
QString endpoint(); // ".../chat/completions" URL, default empty
QString apiKey();   // optional (not needed by most local runners)
QString model();    // e.g. "gpt-4o-mini" or "llama3"; default empty
void setEnabled(bool enabled);
void setEndpoint(const QString &url);
void setApiKey(const QString &key);
void setModel(const QString &model);

// OpenAI chat-completions POST body: {model, messages:[system,user]}.
QByteArray buildChatRequest(const QString &model, const QString &systemPrompt,
                            const QString &userPrompt);
// The assistant's reply text from a chat-completions response, or empty on
// error (the provider's message, if any, is returned via *error).
QString parseChatResponse(const QByteArray &json, QString *error);

// System prompts for each action. They instruct the model to answer in the
// conversation's own language and to return only the requested text.
QString summarizeSystemPrompt();
QString improveSystemPrompt();
QString suggestReplySystemPrompt();
// System prompt for the "unread digest": triage the unread chats into a short,
// prioritised summary in the conversation's language.
QString unreadDigestSystemPrompt();

// Turn the JSON array from ChatNav::unreadDigestScript
// ([{name,count,preview},…]) into a compact "Name (N unread): preview" text for
// the digest prompt. Empty when the array is empty or unparseable. Pure.
QString buildUnreadDigestInput(const QString &unreadJson);

// JS returning a compact "Sender: text" transcript of the last `maxMessages`
// loaded messages of the open chat (no scrolling, no media), for context.
QString readContextScript(int maxMessages);

// Available system memory in MiB, or -1 if it cannot be determined (non-Linux).
// A local model can need several GB, so callers warn when this is low.
long availableMemoryMb();
// Parse MemAvailable (in MiB) from /proc/meminfo contents; -1 if not found.
long memAvailableMbFromProc(const QByteArray &procMeminfo);

} // namespace Ai

class AiClient : public QObject {
  Q_OBJECT
public:
  explicit AiClient(QObject *parent = nullptr);

  // Send system+user prompts to the configured endpoint. Emits completed() with
  // the assistant's text, or failed() with a human-readable reason.
  void complete(const QString &systemPrompt, const QString &userPrompt);

signals:
  void completed(const QString &text);
  void failed(const QString &error);

private:
  QNetworkAccessManager *m_net = nullptr;
};

#endif // AIASSISTANT_H
