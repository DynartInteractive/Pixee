#include "ThumbnailWorker.h"

#include <QBuffer>
#include <QDebug>
#include <QImageReader>
#include <QSize>

ThumbnailWorker::ThumbnailWorker(int targetSize, int jpegQuality, QObject* parent)
    : QObject(parent), _targetSize(targetSize), _jpegQuality(jpegQuality) {}

void ThumbnailWorker::process(QString path, qint64 mtime, qint64 size) {
    QImageReader reader(path);
    reader.setAutoTransform(true);

    const QSize originalSize = reader.size();
    if (originalSize.isValid()) {
        const QSize scaled = originalSize.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        qWarning() << "ThumbnailWorker: failed to read" << path << ":" << reader.errorString();
        emit failed(path);
        return;
    }

    if (image.width() > _targetSize || image.height() > _targetSize) {
        image = image.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", _jpegQuality)) {
        qWarning() << "ThumbnailWorker: JPEG encode failed for" << path;
        emit failed(path);
        return;
    }

    emit generated(path, mtime, size, image.width(), image.height(), image, bytes);
}
