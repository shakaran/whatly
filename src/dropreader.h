#ifndef DROPREADER_H
#define DROPREADER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "dropattach.h"

// Reads the files from a drop and builds the script that hands them to the page.
// The contents are base64-encoded into that script, so a dropped video means
// tens of megabytes of reading, encoding and string building; doing it straight
// in the drop handler froze the window for several seconds with nothing on
// screen to say why. This does the work on its own thread and reports how far it
// has got, so the drop can show a progress bar and the window stays responsive.
class DropReader : public QObject {
  Q_OBJECT

public:
  // Total bytes read into memory for one drop. The files become a base64 string
  // handed to the renderer, so a huge drop would balloon it; over this the
  // remaining files are left out rather than risking the page.
  static constexpr qint64 kMaxTotalBytes = 64LL * 1024 * 1024;

  // What a drop will actually read, worked out from the file sizes before any
  // reading starts, so the caller can size the progress bar and say up front
  // which files did not fit.
  struct Plan {
    QStringList accepted;  // absolute paths, in drop order
    QStringList tooLarge;  // file names left out by kMaxTotalBytes
    qint64 totalBytes = 0; // bytes the accepted files will read
  };
  static Plan plan(const QStringList &paths);

  DropReader(const QStringList &paths, qint64 totalBytes,
             QObject *parent = nullptr);

  // The script for the page, valid once finished() has been emitted. Empty when
  // nothing could be read.
  QString script() const { return m_script; }

signals:
  void progress(qint64 bytesRead, qint64 bytesTotal);
  void finished();

public slots:
  // Runs the read. Meant to be invoked on a worker thread.
  void run();

private:
  QStringList m_paths;
  qint64 m_totalBytes;
  QString m_script;
};

#endif // DROPREADER_H
