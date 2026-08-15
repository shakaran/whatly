#ifndef MEDIASTUCK_H
#define MEDIASTUCK_H

#include <QLatin1String>
#include <QString>

// A media download that never arrives is the one failure WhatsApp Web keeps to
// itself: the placeholder simply stays, the click does nothing, and the reason
// (an expired link, no connection) is never said anywhere the user can see. The
// application cannot see it either — QtWebEngine hands it only JavaScript
// console messages, and a refused download is a network-level failure.
//
// What can be known without any of that is the part that matters: the user
// clicked the same download twice and the file still has not come. That is
// enough for an honest sentence and a first thing to try.
namespace MediaStuck {

// The line the injected watcher prints when it has waited and the media is
// still not there. Kept as a marker rather than a channel of its own: console
// messages already reach the application.
inline constexpr QLatin1String kMarker{"WHATLY_MEDIA_STUCK "};

struct Report {
  int attempts = 0;   // how many times this download was asked for
  bool online = true; // what the page thought of the connection
};

// True for a line the watcher printed, and nothing else.
bool isReport(const QString &consoleMessage);

// The report carried by such a line. A line that is not one, or whose payload
// is broken, gives a Report with no attempts, which advises nothing.
Report parse(const QString &consoleMessage);

enum class Advice {
  None,    // not enough has happened to say anything
  Offline, // there is no connection: it will arrive by itself
  Expired, // asked for repeatedly with a connection: the copy is likely gone
};

Advice adviceFor(const Report &report);

// The sentence for an advice, translated, ready for a toast. Empty for None.
QString text(Advice advice);

// The watcher itself: counts clicks per message, waits, and prints kMarker only
// if the placeholder is still a placeholder afterwards. Idempotent — a second
// injection into the same page does nothing.
QString watcherScript();

} // namespace MediaStuck

#endif // MEDIASTUCK_H
