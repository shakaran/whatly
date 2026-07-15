#pragma once

#include <QString>
#include <QWebEngineProfile>

// Makes new logins appear as "WhatSie for Linux" (or the matching platform)
// in the phone's Linked-Devices list, instead of "Google Chrome (Linux)".
//
// WhatsApp Web derives that label client-side (the WAWebBrowserInfo module;
// WAWebMiscBrowserUtils.info in older builds) and sends it to the phone while
// a device is being linked, so a userscript that decorates the returned
// browser info before the QR code is scanned is enough. The hook is
// defensive: if WhatsApp Web renames its internals the script silently does
// nothing and the label falls back to the stock one.
//
// The label is stored on the phone at link time; toggling the setting only
// affects devices linked afterwards (settings key "identifyInLinkedDevices").
namespace LinkedDeviceName {

// The userscript with the current QSettings baked in. accountLabel, when set,
// is appended so several accounts in the same app show distinct names in the
// phone's linked-devices list ("WhatSie for Linux (Work)"). Used both for
// profile injection (fresh page loads) and for applying a toggle to an
// already-loaded page via QWebEnginePage::runJavaScript.
QString scriptSource(const QString &accountLabel = QString());

// (Re)installs the userscript on the profile for FUTURE page loads according
// to the current QSettings; removes it when disabled. Note: Qt does not
// propagate profile-script changes to an already-created page, so callers
// must also runJavaScript(scriptSource()) on the live page to apply a toggle
// immediately.
void install(QWebEngineProfile *profile,
             const QString &accountLabel = QString());

} // namespace LinkedDeviceName
