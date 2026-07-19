#ifndef MUTEDSTATUS_H
#define MUTEDSTATUS_H

#include <QString>

class QWebEngineProfile;

// Hides the "Muted updates" section in the Status/Updates panel, so muted
// contributors' statuses do not show up at all (issue #242).
//
// WhatsApp Web groups muted statuses under a section whose heading reads
// "Muted updates" in the account's language. There is no stable class or data
// attribute to key on, so the section is found by its heading text (matched
// against the localisations WhatsApp uses) and hidden along with the rows that
// follow it. Everything is wrapped so a missed match only means nothing is
// hidden, never a broken page.
namespace MutedStatus {

bool isEnabled();
void setEnabled(bool enabled);

QString scriptSource();

void install(QWebEngineProfile *profile);

} // namespace MutedStatus

#endif // MUTEDSTATUS_H
