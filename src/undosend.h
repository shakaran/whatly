#ifndef UNDOSEND_H
#define UNDOSEND_H

#include <QString>

class QWebEngineProfile;

// Undo send (idea #7): hold an Enter-sent message for a few seconds behind an
// "Undo" toast, then actually send it, or cancel. Implemented entirely in the
// page: a capture-phase keydown listener swallows the user's Enter and, after
// the delay, re-dispatches a synthetic Enter past its own guard so WhatsApp Web
// sends the (untouched) composer text. Clicking Send still sends immediately.
namespace UndoSend {

bool isEnabled();          // default false (opt-in)
int seconds();             // hold window, default 5, clamped >= 1
void setEnabled(bool enabled);
void setSeconds(int secs);

// The injected script, with the enabled flag, delay and translated toast labels
// substituted. Pure, unit tested.
QString scriptSource();

void install(QWebEngineProfile *profile);

} // namespace UndoSend

#endif // UNDOSEND_H
