#pragma once

#include <QHash>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

// Owns one persistent QWebEngineProfile per in-window account. Each is a
// separate Chromium storage partition — separate cookies, separate WhatsApp
// session — so several accounts can be signed in at once inside one window.
//
// The default account (id "") keeps the storage the app has always used, so
// nothing moves and no one is logged out on upgrade; every other account gets a
// sibling directory keyed on its id. This is orthogonal to the process-level
// --profile flag (see AppProfile): --profile picks which *process namespace* the
// whole set of accounts lives under, and the account id picks one account within
// it.
class WebEngineProfileManager {
public:
    static WebEngineProfileManager &instance();

    // The profile for a given account, created and configured on first request.
    QWebEngineProfile *profileFor(const QString &accountId);

    // The default account's profile. Kept for the many call sites that predate
    // multiple accounts and only ever mean "the current one".
    QWebEngineProfile *profile() const;

    // Re-reads the user-configurable settings — user agent, autoplay, spell
    // check — and the injected scripts, and applies them to every live account
    // profile. Call whenever any of them changes, instead of recreating pages.
    void applyUserSettings();

private:
    WebEngineProfileManager();
    ~WebEngineProfileManager();

    // Storage paths, engine attributes and the injected profile scripts for one
    // account's profile.
    void configureProfile(QWebEngineProfile *profile, const QString &accountId);
    void applyUserSettingsTo(QWebEngineProfile *profile,
                             const QString &accountId);

    // The display name saved for an account id ("" for the default account),
    // used so each account shows a distinct linked-device name on the phone.
    static QString accountLabel(const QString &accountId);

    QHash<QString, QWebEngineProfile *> m_profiles;
    QWebEngineProfile *m_profile = nullptr;   // the default account, id ""
};
