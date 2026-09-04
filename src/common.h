#ifndef COMMON_H
#define COMMON_H
#include <QString>
#include <QIcon>

class QUrl;

// userAgent
extern QString defaultUserAgentStr;

// The origin every page permission belongs to.
extern const QString whatsAppOrigin;

// The application id (the installed .desktop / icon name, net.shakaran.whatly).
// Notification icons and the desktop-file name key off it.
extern const QString kAppId;

// Core identity strings, shared so the QSettings stores, desktop integration and
// About box agree. Usable before main() (the machine-wide settings read them
// early), like the literals they replace.
extern const QString kAppName;        // QSettings application name ("whatly")
extern const QString kAppDisplayName; // user-facing name ("Whatly")
extern const QString kOrgName;        // organization name ("shakaran")
extern const QString kOrgDomain;      // organization domain ("net.shakaran")

// appAutoLock
extern int defaultAppAutoLockDuration;
extern bool defaultAppAutoLock;
extern double defaultZoomFactorMaximized;

QIcon themeIcon(const QString& name, const QString& fallback);
// The application/window icon, resolved from the themed kAppId icon (all sizes +
// SVG) with a multi-size raster fallback, so it stays sharp when scaled (#105).
QIcon appWindowIcon();

// A window request (window.open / target="_blank") whose URL lives on
// web.whatsapp.com is one of WhatsApp's own in-app popups — above all the call
// "Move to new window" popout — and must stay inside Whatly rather than being
// handed to the browser. Anything else is an external link. Pure, unit tested.
bool isInAppPopupUrl(const QUrl &url);

// The account tab tooltip: the WhatsApp Web version (once known) and the build
// token from the page URL, one per line, each omitted when empty. Pure, unit
// tested. Note: the URL "v=" token is a per-load cache-buster, not a version,
// so it is labelled a build token rather than shown as one.
QString accountTabTooltipText(const QString &version, const QString &token);

// What is unread, in as much detail as the page could tell us. The badge can only
// show one number; this is what the tray icon's tooltip says instead, so the one
// number is never the only thing available.
struct UnreadBreakdown {
  int chats = 0;         // chats with something unread
  int messages = 0;      // unread messages in them
  int mutedChats = 0;    // of those chats, the ones told not to interrupt
  int mutedMessages = 0; // and the messages waiting in them
  // Whether the muted figures are known at all: the database walk splits them,
  // the fallback that counts drawn rows cannot, and reporting nought muted when
  // the truth is "not known" would be a lie in the tooltip.
  bool mutedKnown = false;
  // Whether `chats`/`messages` already include the muted ones. They do when the
  // badge is set to count muted (the default); when it is not, the muted figures
  // are a disjoint set kept off the badge, and the tooltip has to add them back
  // to show the real total and a split that does not go negative.
  bool mutedInTotal = true;
};

// The tray icon's tooltip: the app's name, then what is waiting, one thought per
// line, the muted split only when it is known and non-zero. Pure, unit tested.
QString trayTooltipText(const UnreadBreakdown &unread);

// The group-invite code from a link handed to the app: a
// https://chat.whatsapp.com/<code> web link or a whatsapp://chat?code=<code>
// deep link (the form the x-scheme-handler delivers). Empty when the URL is not
// an invite (e.g. a whatsapp://send request). Pure, unit tested (issue #186).
QString inviteCodeFromUrl(const QString &url);

// Page-zoom bounds for Ctrl +/-/0 and the injected zoom buttons; the zoom is
// clamped so it can never become unusably tiny or huge. Pure, so it is unit
// tested directly.
constexpr double kMinZoomFactor = 0.3;
constexpr double kMaxZoomFactor = 3.0;
inline double clampZoom(double factor) {
  return factor < kMinZoomFactor   ? kMinZoomFactor
         : factor > kMaxZoomFactor ? kMaxZoomFactor
                                   : factor;
}


#endif // COMMON_H

