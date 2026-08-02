#ifndef CHATEXPORT_H
#define CHATEXPORT_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

class QJsonArray;

// Export the open conversation (idea #9). The page-side collector auto-scrolls
// the (virtualised) message list from top to bottom, harvesting each rendered
// window and deduplicating by message id, and downloads whatever media is loaded
// as inline data URLs before the row scrolls out and its blob is revoked. It
// runs async and stashes the result on window.__whatlyExport; C++ polls that,
// then writes a WhatsApp-style .txt, a structured .json, and a media/ folder.
// The formatting/parsing helpers are pure and unit-tested.
namespace ChatExport {

struct Message {
  QString ts;        // "HH:MM, D/M/YYYY" as WhatsApp renders it
  QString sender;    // may be empty for outgoing media without a caption
  QString direction; // "in" | "out"
  QString type;      // "text" | "image" | "video" | "audio"
  QString text;      // message body or media caption
  QString mediaFile; // assigned file name under media/, empty if none/undownloaded
  bool mediaMissing = false; // media message whose bytes were not available
};

// The JS that starts the collector (stores results on window.__whatlyExport).
QString collectorScript();
// Small JS reading {status, progress, count} for polling without moving the
// (potentially large) message payload each tick.
QString statusScript();

// Parse the collected messages JSON into records. For each record carrying an
// inline data URL, decode it, pick a file name (NNNN + extension) and put the
// bytes in *mediaData keyed by that file name.
QList<Message> parse(const QJsonArray &arr, QHash<QString, QByteArray> *mediaData);

// Decode "data:<mime>;base64,<payload>" to bytes; *mime receives the media type.
QByteArray decodeDataUrl(const QString &dataUrl, QString *mime);
// Extension (with leading dot) for a mime type, e.g. "image/jpeg" -> ".jpg".
QString extForMime(const QString &mime);
// A file-system-safe version of `name` (for the export folder / file base).
QString sanitizeFileName(const QString &name);

// One WhatsApp-style transcript line for a message.
QString transcriptLine(const Message &m);
// The full .txt transcript, including a short header.
QString buildTranscript(const QString &chatName, const QList<Message> &msgs);
// The structured .json (array of message objects with media file references).
QByteArray buildJson(const QList<Message> &msgs);

} // namespace ChatExport

#endif // CHATEXPORT_H
