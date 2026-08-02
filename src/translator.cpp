#include "translator.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

static const char kEnabledKey[] = "translateEnabled";
static const char kEndpointKey[] = "translateEndpoint";
static const char kApiKeyKey[] = "translateApiKey";
static const char kTargetKey[] = "translateTarget";

namespace {
// A JS double-quoted string literal from arbitrary text.
QString jsString(const QString &value) {
  QString e = value;
  e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  e.replace(QLatin1Char('"'), QLatin1String("\\\""));
  e.replace(QLatin1Char('\n'), QLatin1String("\\n"));
  e.replace(QLatin1Char('\r'), QString());
  return QLatin1Char('"') + e + QLatin1Char('"');
}
} // namespace

namespace Translate {

bool isEnabled() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEnabledKey), false)
      .toBool();
}
QString endpoint() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kEndpointKey))
      .toString()
      .trimmed();
}
QString apiKey() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kApiKeyKey))
      .toString()
      .trimmed();
}
QString targetLang() {
  return SettingsManager::instance()
      .settings()
      .value(QLatin1String(kTargetKey))
      .toString()
      .trimmed();
}
void setEnabled(bool enabled) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEnabledKey),
                                                  enabled);
}
void setEndpoint(const QString &url) {
  SettingsManager::instance().settings().setValue(QLatin1String(kEndpointKey),
                                                  url.trimmed());
}
void setApiKey(const QString &key) {
  SettingsManager::instance().settings().setValue(QLatin1String(kApiKeyKey),
                                                  key.trimmed());
}
void setTargetLang(const QString &code) {
  SettingsManager::instance().settings().setValue(QLatin1String(kTargetKey),
                                                  code.trimmed());
}

QString effectiveTargetLang(const QString &configured, const QString &appLang) {
  if (!configured.trimmed().isEmpty())
    return configured.trimmed().toLower();
  // "es_ES" / "es-ES" -> "es"; "es" -> "es".
  QString base = appLang.trimmed();
  const int cut = base.indexOf(QRegularExpression(QStringLiteral("[_-]")));
  if (cut > 0)
    base = base.left(cut);
  base = base.toLower();
  return base.isEmpty() ? QStringLiteral("en") : base;
}

QByteArray buildRequestBody(const QString &text, const QString &source,
                            const QString &target, const QString &apiKey) {
  QJsonObject o;
  o.insert(QStringLiteral("q"), text);
  o.insert(QStringLiteral("source"),
           source.trimmed().isEmpty() ? QStringLiteral("auto")
                                      : source.trimmed());
  o.insert(QStringLiteral("target"), target);
  o.insert(QStringLiteral("format"), QStringLiteral("text"));
  if (!apiKey.trimmed().isEmpty())
    o.insert(QStringLiteral("api_key"), apiKey.trimmed());
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString parseResponse(const QByteArray &json, QString *error) {
  if (error)
    error->clear();
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error)
      *error = QObject::tr("Unexpected response from the translation service.");
    return QString();
  }
  const QJsonObject o = doc.object();
  // LibreTranslate reports failures as {"error":"…"}.
  if (o.contains(QStringLiteral("error"))) {
    if (error)
      *error = o.value(QStringLiteral("error")).toString();
    return QString();
  }
  const QJsonValue t = o.value(QStringLiteral("translatedText"));
  if (t.isString())
    return t.toString();
  // Batch form: {"translatedText":["…"]}.
  if (t.isArray() && !t.toArray().isEmpty())
    return t.toArray().first().toString();
  if (error)
    *error = QObject::tr("The translation service returned no text.");
  return QString();
}

QString readSelectionScript() {
  return QStringLiteral(
      "(function(){try{return (window.getSelection ? "
      "String(window.getSelection()) : '') || '';}catch(e){return '';}})();");
}

QString readComposerScript() {
  return QStringLiteral(
      "(function(){try{var b=document.querySelector("
      "'footer [contenteditable=\"true\"]')||document.querySelector("
      "'div[data-tab=\"10\"][contenteditable=\"true\"]');"
      "return b?(b.innerText||''):'';}catch(e){return '';}})();");
}

QString replaceComposerScript(const QString &text) {
  QString js = QString::fromLatin1(R"JS(
(function () {
  'use strict';
  try {
    var TEXT = __TEXT__;
    var box = document.querySelector('footer [contenteditable="true"]') ||
              document.querySelector('div[data-tab="10"][contenteditable="true"]');
    if (!box) return;
    box.focus();
    // Select the whole draft, then overwrite it through execCommand so
    // WhatsApp keeps tracking the input (Send button, drafts) as if typed.
    document.execCommand('selectAll', false, null);
    document.execCommand('insertText', false, TEXT);
  } catch (e) { /* never break the page */ }
})();
)JS");
  js.replace(QLatin1String("__TEXT__"), jsString(text));
  return js;
}

QString toastScript(const QString &text) {
  QString js = QString::fromLatin1(R"JS(
(function () {
  'use strict';
  try {
    var TEXT = __TEXT__;
    var old = document.getElementById('whatly-translate-toast');
    if (old) old.remove();
    var t = document.createElement('div');
    t.id = 'whatly-translate-toast';
    t.textContent = TEXT;
    t.style.cssText = 'position:fixed;z-index:100000;max-width:min(560px,80vw);' +
      'background:#202c33;color:#e9edef;padding:10px 14px;border-radius:10px;' +
      'box-shadow:0 6px 22px rgba(0,0,0,.5);font-size:14px;line-height:1.4;' +
      'white-space:pre-wrap;cursor:pointer;';
    // Anchor above the current selection when there is one, else bottom-centre.
    var x = null, y = null;
    try {
      var sel = window.getSelection();
      if (sel && sel.rangeCount) {
        var r = sel.getRangeAt(0).getBoundingClientRect();
        if (r && (r.width || r.height)) { x = r.left; y = r.top; }
      }
    } catch (e) {}
    if (x === null) {
      t.style.left = '50%'; t.style.bottom = '90px';
      t.style.transform = 'translateX(-50%)';
    } else {
      t.style.left = Math.max(8, Math.min(x, window.innerWidth - 320)) + 'px';
      t.style.top = Math.max(8, y - 12) + 'px';
      t.style.transform = 'translateY(-100%)';
    }
    t.onclick = function () { t.remove(); };
    (document.body || document.documentElement).appendChild(t);
    setTimeout(function () { if (t && t.parentNode) t.remove(); }, 12000);
  } catch (e) { /* never break the page */ }
})();
)JS");
  js.replace(QLatin1String("__TEXT__"), jsString(text));
  return js;
}

} // namespace Translate

Translator::Translator(QObject *parent) : QObject(parent) {}

void Translator::translate(const QString &text, const QString &target) {
  const QString url = Translate::endpoint();
  if (url.isEmpty()) {
    emit failed(QObject::tr("No translation endpoint is configured "
                             "(Settings → Translation)."));
    return;
  }
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    emit failed(QObject::tr("Nothing to translate."));
    return;
  }
  if (!m_net)
    m_net = new QNetworkAccessManager(this);

  QNetworkRequest req{QUrl(url)};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json"));
  const QByteArray body =
      Translate::buildRequestBody(trimmed, QString(), target, Translate::apiKey());
  QNetworkReply *reply = m_net->post(req, body);
  const QString original = trimmed;
  connect(reply, &QNetworkReply::finished, this, [this, reply, original]() {
    reply->deleteLater();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      QString err;
      const QString fromBody = Translate::parseResponse(data, &err);
      Q_UNUSED(fromBody);
      emit failed(err.isEmpty() ? reply->errorString() : err);
      return;
    }
    QString err;
    const QString out = Translate::parseResponse(data, &err);
    if (out.isEmpty())
      emit failed(err.isEmpty() ? QObject::tr("Translation failed.") : err);
    else
      emit translated(out, original);
  });
}
