#include "webengineprofilemanager.h"
#include "appprofile.h"
#include "common.h"
#include "linkeddevicename.h"
#include "settingsmanager.h"
#include "chattheme.h"
#include "dictionaries.h"
#include "chatwallpaper.h"
#include "customcss.h"
#include "customjs.h"
#include "privacyblur.h"
#include "webfont.h"
#include "mutedstatus.h"
#include "webtweaks.h"
#include "performance.h"

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
    // The default account. profileFor("") both creates and configures it, and
    // caches it, so m_profile and the map never disagree.
    m_profile = profileFor(QString());
}

QWebEngineProfile *WebEngineProfileManager::profileFor(const QString &accountId) {
    auto it = m_profiles.constFind(accountId);
    if (it != m_profiles.constEnd())
        return it.value();

    // A distinct storage name per account is what makes each a separate Chromium
    // partition. The default account keeps the bare "whatly" (plus the
    // process-level --profile suffix) so its existing session is untouched.
    QString storageName = QStringLiteral("whatly") + AppProfile::suffix();
    if (!accountId.isEmpty())
        storageName += QLatin1Char('-') + accountId;

    auto *profile = new QWebEngineProfile(storageName);
    configureProfile(profile, accountId);
    m_profiles.insert(accountId, profile);
    return profile;
}

void WebEngineProfileManager::configureProfile(QWebEngineProfile *profile,
                                               const QString &accountId) {
    // Derive the default UA from the engine itself *before* we ever override
    // httpUserAgent, so it always matches the installed Chromium version and
    // stays current across Qt WebEngine upgrades. Only worth doing once — every
    // account's engine reports the same UA. The hardcoded value in common.cpp
    // survives only as a fallback if this ever yields nothing.
    if (defaultUserAgentStr.isEmpty() || m_profiles.isEmpty()) {
        const QString engineUA = stripQtWebEngineToken(profile->httpUserAgent());
        if (!engineUA.isEmpty())
            defaultUserAgentStr = engineUA;
        qDebug() << "Engine-derived default UserAgent:" << defaultUserAgentStr;
    }

    const QString dataPath  = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    // Default account keeps ".../QtWebEngine"; a named one gets its own sibling
    // directory, so the sessions never touch.
    QString engineSub = QStringLiteral("/QtWebEngine") + AppProfile::suffix();
    if (!accountId.isEmpty())
        engineSub += QLatin1Char('-') + accountId;

    profile->setPersistentStoragePath(dataPath + engineSub);
    profile->setCachePath(cachePath + engineSub);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

    // Apply the user's HTTP-cache preference (type + max size).
    Performance::applyToProfile(profile);

    qDebug() << "WebEngineProfile" << (accountId.isEmpty() ? "(default)" : accountId)
             << "storage:" << profile->persistentStoragePath();

    auto *s = profile->settings();
    s->setAttribute(QWebEngineSettings::AutoLoadImages,                    true);
    s->setAttribute(QWebEngineSettings::JavascriptEnabled,                 true);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,          true);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled,               true);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,   true);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,     true);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled,                true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,          true);
    s->setAttribute(QWebEngineSettings::LinksIncludedInFocusChain,         false);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled,          false);
    s->setAttribute(QWebEngineSettings::SpatialNavigationEnabled,          true);
    s->setAttribute(QWebEngineSettings::JavascriptCanPaste,                true);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard,      true);

    applyUserSettingsTo(profile, accountId);

    auto *m_profile = profile;   // the scripts below were written against m_profile

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
    // window.__whatlyWsStuck() periodically and reloads when the socket has
    // died or gone silent. Must run in the MainWorld and before page scripts so
    // it patches the constructor WhatsApp actually uses.
    QWebEngineScript wsProbe;
    wsProbe.setName(QStringLiteral("whatly-ws-watchdog"));
    wsProbe.setSourceCode(QStringLiteral(
        "(function(){"
        "  if(window.__whatlyWsPatched)return;"
        "  var Native=window.WebSocket;"
        "  if(!Native)return;"
        "  window.__whatlyWsPatched=true;"
        "  var st={open:0,everOpened:false,last:Date.now()};"
        "  window.__whatlyWs=st;"
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
        "  window.__whatlyWsState=function(){"
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
    qDeleteAll(m_profiles);
}

QWebEngineProfile *WebEngineProfileManager::profile() const {
    return m_profile;
}

void WebEngineProfileManager::applyUserSettings() {
    // Settings are global, so a change applies to every account's profile — but
    // the linked-device name is per account, so each needs its own id.
    for (auto it = m_profiles.constBegin(); it != m_profiles.constEnd(); ++it)
        applyUserSettingsTo(it.value(), it.key());
}

// The default account keeps the bare "Whatly for Linux"; a named one appends
// its tab name. The names live in the same settings the accounts list is saved
// to (see MainWindow::saveAccounts).
QString WebEngineProfileManager::accountLabel(const QString &accountId) {
    if (accountId.isEmpty())
        return QString();
    QSettings &s = SettingsManager::instance().settings();
    const QStringList ids = s.value(QStringLiteral("accounts/ids")).toStringList();
    const QStringList names = s.value(QStringLiteral("accounts/names")).toStringList();
    const int i = ids.indexOf(accountId);
    return (i >= 0 && i < names.size()) ? names.at(i) : QString();
}

void WebEngineProfileManager::applyUserSettingsTo(QWebEngineProfile *profile,
                                                  const QString &accountId) {
    QSettings &s = SettingsManager::instance().settings();

    profile->setHttpUserAgent(
        s.value(QStringLiteral("useragent"), defaultUserAgentStr).toString());

    profile->settings()->setAttribute(
        QWebEngineSettings::PlaybackRequiresUserGesture,
        s.value(QStringLiteral("autoPlayMedia"), false).toBool());

    // Smooth (animated) scrolling, off by default to match how it always
    // behaved. A live QWebEngineSettings attribute, so the toggle takes effect
    // without a reload.
    profile->settings()->setAttribute(
        QWebEngineSettings::ScrollAnimatorEnabled,
        s.value(QStringLiteral("smoothScrolling"), false).toBool());

    // Chromium's spell checker underlines nothing unless it is given a language
    // whose .bdic it can actually find, so an empty list is the same as off. It
    // checks against every language in the list at once, which is the point of
    // letting more than one be selected.
    const QStringList dictionaries = Dictionaries::selectedDictionaries();
    const bool spellCheck =
        s.value(QStringLiteral("spellCheckEnabled"), true).toBool() &&
        !dictionaries.isEmpty();
    profile->setSpellCheckEnabled(spellCheck);
    profile->setSpellCheckLanguages(spellCheck ? dictionaries : QStringList{});

    WebTweaks::install(profile);
    ChatWallpaper::install(profile);
    CustomCss::install(profile);
    CustomJs::install(profile);
    ChatTheme::install(profile);
    PrivacyBlur::install(profile);
    WebFont::install(profile);
    MutedStatus::install(profile);
    LinkedDeviceName::install(profile, accountLabel(accountId));
}
