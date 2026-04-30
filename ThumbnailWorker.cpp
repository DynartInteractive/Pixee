#include "ThumbnailWorker.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QImageReader>
#include <QSize>

namespace {
constexpr int kChunkSize = 64 * 1024;
}

ThumbnailWorker::ThumbnailWorker(int targetSize, int jpegQuality,
                                 QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent),
      _targetSize(targetSize),
      _jpegQuality(jpegQuality),
      _abortVersion(abortVersion) {}

bool ThumbnailWorker::isAborted(int taskVersion) const {
    return _abortVersion && _abortVersion->loadAcquire() != taskVersion;
}

void ThumbnailWorker::process(QString path, qint64 mtime, qint64 size, int taskVersion) {
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    // Read the file ourselves in chunks so we can interrupt at chunk
    // boundaries — QImageReader::read() against a file path can't be aborted.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ThumbnailWorker: failed to open" << path << ":" << file.errorString();
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
            // EOF or read error.
            if (file.error() != QFile::NoError) {
                qWarning() << "ThumbnailWorker: read error on" << path << ":" << file.errorString();
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

    // Decode from the in-memory buffer. setScaledSize before read() still
    // lets the JPEG decoder produce a smaller image directly.
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);

    const QSize originalSize = reader.size();
    if (originalSize.isValid()) {
        const QSize scaled = originalSize.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        qWarning() << "ThumbnailWorker: decode failed for" << path << ":" << reader.errorString();
        emit failed(path);
        return;
    }

    if (image.width() > _targetSize || image.height() > _targetSize) {
        image = image.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer outBuffer(&bytes);
    outBuffer.open(QIODevice::WriteOnly);
    // Preserve transparency for the cache: PNG when there's an alpha channel,
    // JPEG otherwise. The database column is opaque bytes; load auto-detects.
    const bool hasAlpha = image.hasAlphaChannel();
    const char* fmt = hasAlpha ? "PNG" : "JPEG";
    const int quality = hasAlpha ? -1 : _jpegQuality;
    if (!image.save(&outBuffer, fmt, quality)) {
        qWarning() << "ThumbnailWorker: encode failed for" << path << "as" << fmt;
        emit failed(path);
        return;
    }

    emit generated(path, mtime, size, image.width(), image.height(), image, bytes);
}
