#include "ImageLoader.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QImageReader>

#include "IcoUtils.h"

namespace {
constexpr int kChunkSize = 64 * 1024;
}

ImageLoader::ImageLoader(QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent), _abortVersion(abortVersion) {}

bool ImageLoader::isAborted(int taskVersion) const {
    return _abortVersion && _abortVersion->loadAcquire() != taskVersion;
}

void ImageLoader::load(QString path, int taskVersion) {
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ImageLoader: failed to open" << path << ":" << file.errorString();
        emit failed(path);
        return;
    }

    QByteArray data;
    const qint64 fileSize = file.size();
    if (fileSize > 0) {
        data.reserve(static_cast<int>(qMin<qint64>(fileSize, INT_MAX)));
    }
    while (!file.atEnd()) {
        if (isAborted(taskVersion)) {
            emit aborted(path);
            return;
        }
        const QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (file.error() != QFile::NoError) {
                qWarning() << "ImageLoader: read error on" << path << ":" << file.errorString();
                emit failed(path);
                return;
            }
            break;
        }
        data.append(chunk);
    }
    file.close();

    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    // For multi-image ICO files Qt's default reader.read() returns the
    // first directory entry, which is rarely the highest-quality one.
    // Mirror the thumbnail worker's best-pick (largest area, deepest
    // pixel format) so the viewer surfaces the same image the grid
    // shows. Returns null for non-ICO / single-entry files; we fall
    // through to the normal read() in that case.
    QImage image = IcoUtils::pickBestSubImage(reader, path);
    if (image.isNull()) image = reader.read();
    if (image.isNull()) {
        qWarning() << "ImageLoader: decode failed for" << path << ":" << reader.errorString();
        emit failed(path);
        return;
    }
    emit loaded(path, image);
}
