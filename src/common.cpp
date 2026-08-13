#include "common.h"

#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

// Define global variables here to avoid multiple definition errors.
// NOTE: this is only a fallback. At startup WebEngineProfileManager overrides
// defaultUserAgentStr with the engine's own UA (stripped of the QtWebEngine
// token) so the reported Chrome version always matches the installed Chromium
// and auto-updates on Qt WebEngine upgrades.
QString defaultUserAgentStr = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";

const QString whatsAppOrigin = QStringLiteral("https://web.whatsapp.com");

// The application id: the name of the installed .desktop file and icon
// (net.shakaran.whatly). Notification icons, the desktop-file name and launcher
// entries all key off it, so it lives in one place.
const QString kAppId = QStringLiteral("net.shakaran.whatly");

// The core identity strings, in one place because they are reused across the
// QSettings stores (including the machine-wide ones read before QApplication
// exists), the desktop integration and the About box. These are plain globals so
// they are usable before main(), like the literals they replace.
const QString kAppName = QStringLiteral("whatly");        // QSettings/app name
const QString kAppDisplayName = QStringLiteral("Whatly"); // shown to the user
const QString kOrgName = QStringLiteral("shakaran");
const QString kOrgDomain = QStringLiteral("net.shakaran");

int defaultAppAutoLockDuration = 30;
bool defaultAppAutoLock = false;
double defaultZoomFactorMaximized = 1.00;

QIcon themeIcon(const QString& name, const QString& fallback) {
  return QIcon::fromTheme(name, QIcon(fallback));
}

bool isInAppPopupUrl(const QUrl &url) {
  return url.isValid() && url.host() == QLatin1String("web.whatsapp.com");
}

QString trayTooltipText(const UnreadBreakdown &unread) {
  QStringList lines;
  lines << kAppDisplayName;
  if (unread.chats <= 0) {
    lines << QObject::tr("Nothing unread");
    return lines.join(QLatin1Char('\n'));
  }

  // Singular and plural spelled out rather than left to %n: %n picks a plural form
  // only once a translation is loaded, so the source language — the one an English
  // desktop reads — would say "1 chats".
  //
  // The whole of it first, because that is the number on the badge's shoulders.
  if (unread.messages == 1 && unread.chats == 1)
    lines << QObject::tr("1 unread message in 1 chat");
  else if (unread.messages == 1)
    lines << QObject::tr("1 unread message in %1 chats").arg(unread.chats);
  else if (unread.chats == 1)
    lines << QObject::tr("%1 unread messages in 1 chat").arg(unread.messages);
  else
    lines << QObject::tr("%1 unread messages in %2 chats")
                 .arg(unread.messages)
                 .arg(unread.chats);

  // Then the split, which is the point of a tooltip: a hundred messages waiting in
  // muted chats and three in the rest is a different morning from the other way
  // round, and the badge cannot tell the two apart.
  if (unread.mutedKnown && unread.mutedChats > 0) {
    lines << (unread.mutedChats == 1
                  ? QObject::tr("%1 in 1 muted chat").arg(unread.mutedMessages)
                  : QObject::tr("%1 in %2 muted chats")
                        .arg(unread.mutedMessages)
                        .arg(unread.mutedChats));
    const int otherChats = unread.chats - unread.mutedChats;
    const int otherMessages = unread.messages - unread.mutedMessages;
    if (otherChats == 1)
      lines << QObject::tr("%1 in 1 chat that is not muted").arg(otherMessages);
    else if (otherChats > 1)
      lines << QObject::tr("%1 in %2 chats that are not muted")
                   .arg(otherMessages)
                   .arg(otherChats);
  }
  return lines.join(QLatin1Char('\n'));
}

QString accountTabTooltipText(const QString &version, const QString &token) {
  QStringList lines;
  if (!version.isEmpty())
    lines << QObject::tr("WhatsApp Web %1").arg(version);
  if (!token.isEmpty())
    lines << QObject::tr("Build token: %1").arg(token);
  return lines.join(QLatin1Char('\n'));
}

QString inviteCodeFromUrl(const QString &url) {
  const QString u = url.trimmed();
  // A web invite link: https://chat.whatsapp.com/<code> (older links carry an
  // extra /invite/ segment). Checked first so it wins regardless of scheme.
  static const QRegularExpression web(
      QStringLiteral("chat\\.whatsapp\\.com/(?:invite/)?([A-Za-z0-9._-]+)"));
  QRegularExpressionMatch m = web.match(u);
  if (m.hasMatch())
    return m.captured(1);
  // The deep link the x-scheme-handler delivers: whatsapp://chat?code=<code>.
  // Scoped to the "chat" action so a whatsapp://send?...&text=code=... request
  // can never be mistaken for an invite.
  if (u.startsWith(QLatin1String("whatsapp://chat"), Qt::CaseInsensitive)) {
    static const QRegularExpression codeParam(
        QStringLiteral("[?&]code=([A-Za-z0-9._-]+)"));
    m = codeParam.match(u);
    if (m.hasMatch())
      return m.captured(1);
  }
  return QString();
}
