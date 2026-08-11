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

// JS that returns a JSON array of the currently-listed unread chats with a short
// preview of their latest message, most recent first, capped at `limit`:
// [{"name": "...", "count": N, "preview": "..."}]. Same locale-independent badge
// detection as unreadChatsScript; the preview is the row's own text minus the
// name, so it needs no obfuscated class. Feeds the AI "unread digest" (idea #5).
QString unreadDigestScript(int limit);

// JS that answers how much is unread, as JSON: {"chats": N, "messages": M}.
//
// It reads WhatsApp Web's own IndexedDB (`model-storage`, store `chat`) rather
// than the chat list, because the list is virtualised: a session with 592 chats
// keeps about 66 of them in the DOM, so counting rows answers "unread among the
// ones currently drawn" and changes as the list is scrolled. The database holds
// every chat whatever is on screen.
//
// Archived chats are left out — they are the ones deliberately put away, and
// they are not in the list the count sits beside. Muted ones are counted: they
// still carry a badge in that list, and a count that disagreed with what the
// user can see would be the bug this replaces.
//
// Falls back to counting the drawn rows if the database is not readable, which
// is the honest degradation: WhatsApp's schema is theirs to change.
// `includeMuted` and `includeArchived` are the user's answers to what the badge
// should count; the list fallback cannot honour either, since a drawn row says
// nothing about muting and the archived pile is not in that list at all.
QString unreadSummaryScript(bool includeMuted, bool includeArchived);

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
