#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Inline translation (idea #6) through a LibreTranslate-compatible endpoint. The
// HTTP request is made from C++, not from the page, so the endpoint URL and the
// API key never touch WhatsApp Web. Settings, request building and response
// parsing are pure and unit-tested; only send() needs the network.
namespace Translate {

bool isEnabled();     // default false (opt-in)
QString endpoint();   // LibreTranslate "/translate" URL, default empty
QString apiKey();     // optional
QString targetLang(); // configured target code, empty = follow the app language
void setEnabled(bool enabled);
void setEndpoint(const QString &url);
void setApiKey(const QString &key);
void setTargetLang(const QString &code);

// Resolve the effective 2-letter target language: the configured code if set,
// otherwise the base of the app UI language ("es_ES" -> "es"), falling back to
// "en" when neither yields anything usable.
QString effectiveTargetLang(const QString &configured, const QString &appLang);

// LibreTranslate POST body: {"q","source","target","format","api_key"}. An
// empty `source` requests auto-detection; an empty `apiKey` is omitted.
QByteArray buildRequestBody(const QString &text, const QString &source,
                            const QString &target, const QString &apiKey);

// The translated text from a LibreTranslate JSON reply, or an empty string on
// error (the endpoint's error message, if present, is returned via *error).
QString parseResponse(const QByteArray &json, QString *error);

// JS reading the current text selection as a plain string (for runJavaScript).
QString readSelectionScript();
// JS reading the composer's current text (for runJavaScript).
QString readComposerScript();
// JS replacing the composer's text with `text` (kept in WhatsApp's input loop
// via execCommand, so the Send button enables). The text is JSON-escaped.
QString replaceComposerScript(const QString &text);
// JS showing a small dismissable toast with `text` near the current selection
// (or bottom-centre if there is none). The text is JSON-escaped.
QString toastScript(const QString &text);

} // namespace Translate

class Translator : public QObject {
  Q_OBJECT
public:
  explicit Translator(QObject *parent = nullptr);

  // Translate `text` into `target` (source auto-detected). Emits translated()
  // with the original echoed back, or failed() with a human-readable reason.
  void translate(const QString &text, const QString &target);

signals:
  void translated(const QString &translatedText, const QString &original);
  void failed(const QString &error);

private:
  QNetworkAccessManager *m_net = nullptr;
};

#endif // TRANSLATOR_H
