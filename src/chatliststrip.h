#ifndef CHATLISTSTRIP_H
#define CHATLISTSTRIP_H

#include <QString>
#include <QWebEngineProfile>

// Collapses WhatsApp Web's chat list into a narrow strip of profile pictures,
// handing the width it was using to the open conversation. A toggle, not a
// preference to be set once: it is driven from a button in WhatsApp's own rail
// and from Ctrl+B, and the current state is remembered.
//
// Implemented the same way as PrivacyBlur — plain CSS in an injected <style> —
// so there is no scripting to keep in sync with WhatsApp's React tree, and a
// rule that stops matching simply stops collapsing rather than breaking the
// page.
namespace ChatListStrip {

bool isCollapsed();
void setCollapsed(bool collapsed);

// The script that adds or removes the stylesheet to match the current state.
QString scriptSource();

// (Re)installs the stylesheet on the profile for FUTURE page loads. As with
// WebTweaks, Qt does not propagate profile-script changes to an already-created
// page, so callers must also runJavaScript(scriptSource()) on the live page for
// a toggle to take effect immediately.
void install(QWebEngineProfile *profile);

} // namespace ChatListStrip

#endif // CHATLISTSTRIP_H
