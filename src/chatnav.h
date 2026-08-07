#ifndef CHATNAV_H
#define CHATNAV_H

#include <QString>

// Small JavaScript builders for navigating WhatsApp Web's chat list from the
// Qt side. Kept free of any WebEngine dependency so they can be unit tested; the
// runtime behaviour is verified live (WhatsApp Web's DOM is obfuscated and
// virtualised). Idea #3: recent unread chats in the tray menu.
namespace ChatNav {

// JS that returns a JSON array of the currently-listed unread chats, most recent
// first, capped at `limit`: [{"name": "...", "count": N}]. Unread is detected by
// the numeric count pill on the row, which is locale-independent.
QString unreadChatsScript(int limit);

// JS that opens the chat whose row title matches `name` exactly, by dispatching
// the pointer/mouse sequence WhatsApp Web's list rows react to (a plain click is
// ignored). Returns "ok" / "not-found" / "no-pane". The name is JSON-escaped, so
// any characters are safe.
QString openChatByNameScript(const QString &name);

// JS that puts the keyboard into WhatsApp Web's own chat-list search box and
// selects whatever is in it, so typing replaces it — what Ctrl+F does in a
// browser. WhatsApp Web itself binds no key for this: in a browser Ctrl+F is the
// browser's find bar, and Qt WebEngine has none, so the key did nothing at all.
// Returns "ok" / "not-found".
QString focusSearchScript();

} // namespace ChatNav

#endif // CHATNAV_H
