#include "webengineprofilemanager.h"
#include "common.h"
#include "settingsmanager.h"
#include "webtweaks.h"

#include <QDebug>
#include <QStandardPaths>
#include <QWebEngineScriptCollection>

WebEngineProfileManager &WebEngineProfileManager::instance() {
    static WebEngineProfileManager inst;
    return inst;
}

// Strip the "QtWebEngine/x.y.z" token from an engine-provided User-Agent so
// WhatsApp Web sees a plain Chrome UA. Qt's built-in UA always carries the
// Chrome version that matches the installed Chromium, so the result tracks the
// engine automatically (no hardcoded version to go stale on Qt upgrades).
static QString stripQtWebEngineToken(QString userAgent) {
    const int idx = userAgent.indexOf(QStringLiteral("QtWebEngine"));
    if (idx == -1)
        return userAgent; // already clean (e.g. a user-defined UA)

    // Token runs from "QtWebEngine" up to and including the following space.
    int end = userAgent.indexOf(QLatin1Char(' '), idx);
    if (end == -1)
        end = userAgent.length();
    else
        end += 1; // also drop the separating space
    userAgent.remove(idx, end - idx);
    return userAgent.simplified();
}

WebEngineProfileManager::WebEngineProfileManager() {
    // Named profile → persistent storage is enabled automatically.
    m_profile = new QWebEngineProfile(QStringLiteral("whatsie"));

    // Derive the default UA from the engine itself *before* we ever override
    // httpUserAgent, so it always matches the installed Chromium version and
    // stays current across Qt WebEngine upgrades. The hardcoded value in
    // common.cpp only survives as a fallback if this ever yields nothing.
    const QString engineUA = stripQtWebEngineToken(m_profile->httpUserAgent());
    if (!engineUA.isEmpty())
        defaultUserAgentStr = engineUA;
    qDebug() << "Engine-derived default UserAgent:" << defaultUserAgentStr;

    const QString dataPath  = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    m_profile->setPersistentStoragePath(dataPath  + QStringLiteral("/QtWebEngine"));
    m_profile->setCachePath(cachePath + QStringLiteral("/QtWebEngine"));
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

    qDebug() << "WebEngineProfile persistent storage:" << m_profile->persistentStoragePath();
    qDebug() << "WebEngineProfile cache path:"         << m_profile->cachePath();

    auto *s = m_profile->settings();
    s->setAttribute(QWebEngineSettings::AutoLoadImages,                    true);
    s->setAttribute(QWebEngineSettings::JavascriptEnabled,                 true);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,          true);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled,               true);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,   true);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,     true);
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled,             false);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled,                true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,          true);
    s->setAttribute(QWebEngineSettings::LinksIncludedInFocusChain,         false);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled,          false);
    s->setAttribute(QWebEngineSettings::SpatialNavigationEnabled,          true);
    s->setAttribute(QWebEngineSettings::JavascriptCanPaste,                true);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard,      true);

    applyUserSettings();

    // WhatsApp Web calls navigator.storage.persist() at startup and logs a
    // non-fatal error when it returns false (QtWebEngine never auto-grants it).
    // Intercept the call at document-creation time — before any page JS runs —
    // so the promise always resolves true.  This also suppresses the follow-on
    // "[storage] storage bucket persistence denied" console spam.
    QWebEngineScript persistScript;
    persistScript.setName(QStringLiteral("grant-storage-persistence"));
    persistScript.setSourceCode(QStringLiteral(
        "(function(){"
        "  if(navigator.storage&&navigator.storage.persist){"
        "    Object.defineProperty(navigator.storage,'persist',{"
        "      value:function(){return Promise.resolve(true);},"
        "      writable:true,configurable:true"
        "    });"
        "  }"
        "  if(navigator.storage&&navigator.storage.persisted){"
        "    Object.defineProperty(navigator.storage,'persisted',{"
        "      value:function(){return Promise.resolve(true);},"
        "      writable:true,configurable:true"
        "    });"
        "  }"
        "})();"));
    persistScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    persistScript.setWorldId(QWebEngineScript::MainWorld);
    persistScript.setRunsOnSubFrames(false);
    m_profile->scripts()->insert(persistScript);

    // Connection watchdog probe: wrap window.WebSocket at document creation so
    // we can observe WhatsApp's connection. The native side polls
    // window.__whatsieWsStuck() periodically and reloads when the socket has
    // died or gone silent. Must run in the MainWorld and before page scripts so
    // it patches the constructor WhatsApp actually uses.
    QWebEngineScript wsProbe;
    wsProbe.setName(QStringLiteral("whatsie-ws-watchdog"));
    wsProbe.setSourceCode(QStringLiteral(
        "(function(){"
        "  if(window.__whatsieWsPatched)return;"
        "  var Native=window.WebSocket;"
        "  if(!Native)return;"
        "  window.__whatsieWsPatched=true;"
        "  var st={open:0,everOpened:false,last:Date.now()};"
        "  window.__whatsieWs=st;"
        "  function P(u,pr){"
        "    var ws=pr!==undefined?new Native(u,pr):new Native(u);"
        "    try{"
        "      ws.addEventListener('open',function(){st.open++;st.everOpened=true;st.last=Date.now();});"
        "      ws.addEventListener('message',function(){st.last=Date.now();});"
        "      var dec=function(){if(st.open>0)st.open--;};"
        "      ws.addEventListener('close',dec);"
        "      ws.addEventListener('error',dec);"
        "    }catch(e){}"
        "    return ws;"
        "  }"
        "  P.prototype=Native.prototype;"
        "  P.CONNECTING=Native.CONNECTING;P.OPEN=Native.OPEN;"
        "  P.CLOSING=Native.CLOSING;P.CLOSED=Native.CLOSED;"
        "  window.WebSocket=P;"
        "  window.__whatsieWsState=function(){"
        "    if(!st.everOpened||!navigator.onLine)return 'idle';" // connecting/offline: neutral
        "    if(st.open<=0)return 'stuck';"          // socket died and never reopened
        "    if((Date.now()-st.last)>90000)return 'stuck';" // socket open but silent >90s
        "    return 'ok';"                           // connected and active
        "  };"
        "})();"));
    wsProbe.setInjectionPoint(QWebEngineScript::DocumentCreation);
    wsProbe.setWorldId(QWebEngineScript::MainWorld);
    wsProbe.setRunsOnSubFrames(false);
    m_profile->scripts()->insert(wsProbe);
}

WebEngineProfileManager::~WebEngineProfileManager() {
    delete m_profile;
}

QWebEngineProfile *WebEngineProfileManager::profile() const {
    return m_profile;
}

void WebEngineProfileManager::applyUserSettings() {
    QSettings &s = SettingsManager::instance().settings();

    m_profile->setHttpUserAgent(
        s.value(QStringLiteral("useragent"), defaultUserAgentStr).toString());

    m_profile->settings()->setAttribute(
        QWebEngineSettings::PlaybackRequiresUserGesture,
        s.value(QStringLiteral("autoPlayMedia"), false).toBool());

    WebTweaks::install(m_profile);
}
