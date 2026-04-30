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

    QImage image;

    // ICO files contain a directory of sub-images. Qt's ICO handler reads
    // sizes from those directory entries, but the field is one unsigned
    // byte — `0x00` actually means 256 px, and PNG-encoded entries can lie
    // outright. Asking reader.size() per entry then picking the largest
    // misses the high-res entry whenever it's reported as 0×0.
    //
    // Workaround: actually decode every sub-image and compare the resulting
    // QImage sizes. N decodes for N entries, but the file is already in an
    // in-memory buffer so it's cheap.
    if (path.endsWith(".ico", Qt::CaseInsensitive) && reader.imageCount() > 1) {
        // Most ICO files contain several entries at the same dimensions but
        // different bit depths (e.g. 48×48 in 256-color, 24-bit, and 32-bit
        // forms) — by convention the higher-quality versions come last in
        // the directory. Score by area first; on a tie, prefer higher
        // QImage::depth(), and `>=` lets later entries with the same depth
        // win too (covers the case where the handler normalises everything
        // to ARGB32 and depth-tiebreak alone wouldn't help).
        int bestArea = 0;
        int bestDepth = 0;
        for (int i = 0; i < reader.imageCount(); ++i) {
            if (!reader.jumpToImage(i)) continue;
            const QImage candidate = reader.read();
            if (candidate.isNull()) continue;
            const int area = candidate.width() * candidate.height();
            const int depth = candidate.depth();
            if (area > bestArea
                    || (area == bestArea && depth >= bestDepth)) {
                bestArea = area;
                bestDepth = depth;
                image = candidate;
            }
        }
    }

    if (image.isNull()) {
        const QSize originalSize = reader.size();
        // Only ask the decoder to pre-scale when downsizing — letting the
        // decoder upscale for us applies a linear-style filter that softens
        // pixel art. We'll handle upscaling ourselves below.
        if (originalSize.isValid()
                && (originalSize.width() > _targetSize || originalSize.height() > _targetSize)) {
            const QSize scaled = originalSize.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio);
            reader.setScaledSize(scaled);
        }
        image = reader.read();
    }

    if (image.isNull()) {
        qWarning() << "ThumbnailWorker: decode failed for" << path << ":" << reader.errorString();
        emit failed(path);
        return;
    }

    if (image.width() > _targetSize || image.height() > _targetSize) {
        // Downscale: bilinear so big photos look smooth.
        image = image.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else if (image.width() < _targetSize && image.height() < _targetSize) {
        // Source smaller than target on both axes — upscale with
        // nearest-neighbor so pixel art (icons, sprites, etc.) stays crisp.
        image = image.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
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
