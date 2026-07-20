#ifndef CANNEDRESPONSES_H
#define CANNEDRESPONSES_H

#include <QList>
#include <QString>

// Saved replies ("canned responses"): short texts you send often. They are
// stored per account and surface in the command palette as "Insert: <title>",
// which types the text into WhatsApp Web's message box — so a stock reply is
// Ctrl+K, a few letters, Enter.
namespace CannedResponses {

struct Response {
  QString title; // shown in the palette
  QString text;  // inserted into the composer
};

QList<Response> all();
void setAll(const QList<Response> &responses);
void add(const QString &title, const QString &text);
void removeAt(int index);

// JavaScript that inserts `text` at the caret in the message box. Pure, and
// escaped so any quotes/newlines in the text are safe.
QString insertScript(const QString &text);

} // namespace CannedResponses

#endif // CANNEDRESPONSES_H
