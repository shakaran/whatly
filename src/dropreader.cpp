#include "dropreader.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace {
// Read in slices rather than in one go, so a single large file still moves the
// progress bar instead of jumping from nothing to done.
constexpr qint64 kChunkBytes = 1LL * 1024 * 1024;
} // namespace

DropReader::Plan DropReader::plan(const QStringList &paths) {
  Plan p;
  for (const QString &path : paths) {
    const QFileInfo info(path);
    // Directories, and empty or vanished files, have nothing to attach.
    if (!info.isFile() || info.size() <= 0)
      continue;
    if (p.totalBytes + info.size() > kMaxTotalBytes) {
      p.tooLarge.append(info.fileName());
      continue;
    }
    p.totalBytes += info.size();
    p.accepted.append(info.absoluteFilePath());
  }
  return p;
}

DropReader::DropReader(const QStringList &paths, qint64 totalBytes,
                       QObject *parent)
    : QObject(parent), m_paths(paths), m_totalBytes(totalBytes) {}

void DropReader::run() {
  QList<DropAttach::File> files;
  QMimeDatabase mimeDb;
  qint64 done = 0;

  for (const QString &path : m_paths) {
    if (m_cancelled.loadRelaxed())
      return; // torn down mid-read: drop everything and let the thread end
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QByteArray data;
    data.reserve(static_cast<int>(file.size()));
    bool readOk = true;
    while (!file.atEnd()) {
      if (m_cancelled.loadRelaxed())
        return;
      const QByteArray chunk = file.read(kChunkBytes);
      if (chunk.isEmpty()) {
        readOk = false; // read error part-way through
        break;
      }
      data.append(chunk);
      done += chunk.size();
      emit progress(done, m_totalBytes);
    }
    // Skip an unreadable or partially-read file entirely rather than sending a
    // truncated attachment that looks complete (e.g. a video cut off mid-way).
    if (!readOk || data.isEmpty())
      continue;

    const QFileInfo info(path);
    const QString type =
        mimeDb.mimeTypeForFileNameAndData(info.fileName(), data).name();
    files.append({info.fileName(), type, QString::fromLatin1(data.toBase64())});
  }

  // Built here too: the script embeds every file's base64, so assembling it is
  // as expensive as the reading and belongs off the UI thread as well.
  m_script = DropAttach::scriptSource(files);
  emit finished();
}
