#include "PreviewReader.h"

#include <QImageReader>

PreviewReader::PreviewReader(QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent), _abortVersion(abortVersion) {}

bool PreviewReader::isAborted(int taskVersion) const {
    return _abortVersion && _abortVersion->loadAcquire() != taskVersion;
}

void PreviewReader::read(QString path, int taskVersion) {
    // Check before the read as well as after: a burst of selection changes can
    // queue several of these, and the ones already stale should not touch the
    // disk at all.
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    QImageReader reader(path);
    // Match the viewer, which already honours EXIF orientation. It makes no
    // difference to the bins, but it keeps the two paths from diverging.
    reader.setAutoTransform(true);

    const QImage preview = reader.read();
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }
    if (preview.isNull()) {
        emit failed(path);
        return;
    }
    emit ready(path, preview);
}
