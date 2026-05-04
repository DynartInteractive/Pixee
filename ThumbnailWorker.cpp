#include "ThumbnailWorker.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QImageReader>
#include <QSize>
#include <QTransform>

#include "IcoUtils.h"

namespace {
constexpr int kChunkSize = 64 * 1024;
// Enough to span the JPEG SOI + APP1/EXIF + embedded thumbnail. APP1 is
// length-bounded by the 16-bit segment length field (~64KB), so 512KB is
// plenty of headroom for any preceding APP0/JFIF and the EXIF block.
constexpr qint64 kExifProbeSize = 512 * 1024;

struct ExifThumbnail {
    QByteArray bytes;     // standalone JPEG bytes; empty if not found
    int orientation = 1;  // 1..8 EXIF orientation tag value
};

inline quint16 read16(const uchar* p, bool le) {
    return le ? quint16(p[0] | (quint16(p[1]) << 8))
              : quint16((quint16(p[0]) << 8) | p[1]);
}
inline quint32 read32(const uchar* p, bool le) {
    return le
        ? (p[0] | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24))
        : ((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3]);
}

// Parse `data` as a JPEG and pull out the embedded EXIF thumbnail and the
// orientation tag. Defensive at every step — never reads past the end of
// `data`. Returns an empty bytes field if anything looks off.
//
// Why we reimplement instead of leaning on QImageReader:
//   - QImageReader has no API to grab the EXIF thumbnail directly.
//   - We want to decide based on the EXIF *before* paying the cost of
//     reading the full file off SMB; QImageReader needs the whole stream.
ExifThumbnail findExifThumbnail(const QByteArray& data) {
    ExifThumbnail result;
    const int n = data.size();
    if (n < 4) return result;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());

    // JPEG SOI.
    if (p[0] != 0xFF || p[1] != 0xD8) return result;

    // Walk JPEG segments to find APP1 (FFE1) carrying the "Exif\0\0" magic.
    int tiffOffset = -1;
    int tiffLen = 0;
    int i = 2;
    while (i + 3 < n) {
        if (p[i] != 0xFF) break;
        const uchar marker = p[i + 1];
        // SOS = compressed scan data; nothing useful past here. EOI ends.
        if (marker == 0xDA || marker == 0xD9) break;
        // Standalone markers (no length payload): RST0..7 (D0..D7), TEM (01).
        if ((marker & 0xF8) == 0xD0 || marker == 0x01) { i += 2; continue; }
        const int segLen = (int(p[i + 2]) << 8) | int(p[i + 3]);
        if (segLen < 2 || i + 2 + segLen > n) break;
        if (marker == 0xE1) {
            const int payload = i + 4;
            if (payload + 6 <= n
                    && p[payload + 0] == 'E' && p[payload + 1] == 'x'
                    && p[payload + 2] == 'i' && p[payload + 3] == 'f'
                    && p[payload + 4] == 0   && p[payload + 5] == 0) {
                tiffOffset = payload + 6;
                tiffLen    = segLen - 2 - 6;
                break;
            }
        }
        i += 2 + segLen;
    }
    if (tiffOffset < 0 || tiffLen < 8) return result;
    if (tiffOffset + tiffLen > n) return result;

    const uchar* tiff = p + tiffOffset;
    bool le;
    if (tiff[0] == 'I' && tiff[1] == 'I') le = true;
    else if (tiff[0] == 'M' && tiff[1] == 'M') le = false;
    else return result;
    if (read16(tiff + 2, le) != 0x002A) return result;

    const quint32 ifd0Offset = read32(tiff + 4, le);
    if (ifd0Offset + 2 > quint32(tiffLen)) return result;

    const quint16 ifd0Entries = read16(tiff + ifd0Offset, le);
    const quint32 ifd0End = ifd0Offset + 2 + quint32(ifd0Entries) * 12;
    if (ifd0End + 4 > quint32(tiffLen)) return result;

    // Orientation lives in IFD0 (tag 0x0112). Type SHORT, count 1, so the
    // value sits in the low/high 2 bytes of the entry's value/offset slot.
    for (int e = 0; e < ifd0Entries; ++e) {
        const uchar* entry = tiff + ifd0Offset + 2 + e * 12;
        if (read16(entry + 0, le) == 0x0112) {
            const quint16 v = read16(entry + 8, le);
            if (v >= 1 && v <= 8) result.orientation = int(v);
            break;
        }
    }

    // IFD1 (the "next IFD" pointer right after IFD0's entries) holds the
    // embedded thumbnail's offset (0x0201) and length (0x0202).
    const quint32 ifd1Offset = read32(tiff + ifd0End, le);
    if (ifd1Offset == 0 || ifd1Offset + 2 > quint32(tiffLen)) return result;
    const quint16 ifd1Entries = read16(tiff + ifd1Offset, le);
    if (ifd1Offset + 2 + quint32(ifd1Entries) * 12 > quint32(tiffLen)) return result;

    quint32 thumbOff = 0;
    quint32 thumbLen = 0;
    for (int e = 0; e < ifd1Entries; ++e) {
        const uchar* entry = tiff + ifd1Offset + 2 + e * 12;
        const quint16 tag = read16(entry + 0, le);
        if (tag == 0x0201) thumbOff = read32(entry + 8, le);
        else if (tag == 0x0202) thumbLen = read32(entry + 8, le);
    }
    if (thumbOff == 0 || thumbLen == 0) return result;
    if (quint64(thumbOff) + thumbLen > quint64(tiffLen)) return result;

    result.bytes = QByteArray(reinterpret_cast<const char*>(tiff + thumbOff), int(thumbLen));
    return result;
}

QImage applyExifOrientation(const QImage& src, int orientation) {
    if (orientation <= 1 || src.isNull()) return src;
    QTransform t;
    switch (orientation) {
    case 3: t.rotate(180); break;
    case 6: t.rotate(90);  break;
    case 8: t.rotate(-90); break;
    // Mirror cases (2/4/5/7) are rare for camera JPEGs; fall through and
    // return the unrotated thumbnail rather than misorient it.
    default: return src;
    }
    return src.transformed(t, Qt::SmoothTransformation);
}

bool readChunked(QFile& file, QByteArray& data, qint64 limit, ThumbnailWorker* worker,
                 int taskVersion, const QString& path, bool& aborted, bool& failed) {
    aborted = false;
    failed = false;
    while (!file.atEnd() && (limit < 0 || qint64(data.size()) < limit)) {
        if (worker->isAbortedExternal(taskVersion)) {
            aborted = true;
            return false;
        }
        const QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (file.error() != QFile::NoError) {
                qWarning() << "ThumbnailWorker: read error on" << path << ":" << file.errorString();
                failed = true;
                return false;
            }
            break;
        }
        data.append(chunk);
    }
    return true;
}

} // namespace

ThumbnailWorker::ThumbnailWorker(int targetSize, int jpegQuality,
                                 QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent),
      _targetSize(targetSize),
      _jpegQuality(jpegQuality),
      _abortVersion(abortVersion) {}

bool ThumbnailWorker::isAborted(int taskVersion) const {
    if (_abortVersion && _abortVersion->loadAcquire() != taskVersion) return true;
    return _abortRequested.loadAcquire() != 0;
}

bool ThumbnailWorker::isAbortedExternal(int taskVersion) const {
    return isAborted(taskVersion);
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

    // Phase 1: read just enough of the head to scan for an embedded EXIF
    // thumbnail. Camera JPEGs carry a small ready-made preview inside their
    // APP1 segment; using it skips reading + decoding the multi-megapixel
    // main image — a huge win on slow shares.
    bool wasAborted = false, wasFailed = false;
    if (!readChunked(file, data, kExifProbeSize, this, taskVersion, path, wasAborted, wasFailed)) {
        if (wasAborted) emit aborted(path);
        else if (wasFailed) emit failed(path);
        return;
    }

    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    QImage image;
    bool fromExif = false;

    // Try the embedded thumbnail. Only accept it if its longer edge is at
    // least 3/4 of the target size — anything smaller would upscale to a
    // visibly soft tile, defeating the point of nice big thumbnails.
    const ExifThumbnail exif = findExifThumbnail(data);
    if (!exif.bytes.isEmpty()) {
        QByteArray thumbBytes = exif.bytes;  // QBuffer needs a non-const pointer
        QBuffer thumbBuf(&thumbBytes);
        thumbBuf.open(QIODevice::ReadOnly);
        QImageReader thumbReader(&thumbBuf);
        QImage candidate = thumbReader.read();
        const int minLong = (_targetSize * 3) / 4;
        if (!candidate.isNull()
                && qMax(candidate.width(), candidate.height()) >= minLong) {
            image = applyExifOrientation(candidate, exif.orientation);
            fromExif = true;
        }
    }

    if (image.isNull()) {
        // Phase 2: no usable embedded thumbnail. Read the rest of the file
        // (continuing from where Phase 1 stopped) and run the full decode.
        if (!readChunked(file, data, -1, this, taskVersion, path, wasAborted, wasFailed)) {
            if (wasAborted) emit aborted(path);
            else if (wasFailed) emit failed(path);
            return;
        }
        if (isAborted(taskVersion)) {
            emit aborted(path);
            return;
        }

        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);

        image = IcoUtils::pickBestSubImage(reader, path);

        if (image.isNull()) {
            const QSize originalSize = reader.size();
            // Only ask the decoder to pre-scale when downsizing — letting the
            // decoder upscale for us applies a linear-style filter that
            // softens pixel art. We'll handle upscaling ourselves below.
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
    }

    file.close();

    // Final scale to target. EXIF thumbnails are JPEG photos that may need
    // a slight smooth upscale; the full-decode path keeps the existing
    // smooth-down / nearest-up convention so pixel art doesn't get blurred.
    if (fromExif) {
        if (image.width() != _targetSize && image.height() != _targetSize) {
            image = image.scaled(_targetSize, _targetSize,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    } else if (image.width() > _targetSize || image.height() > _targetSize) {
        image = image.scaled(_targetSize, _targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else if (image.width() < _targetSize && image.height() < _targetSize) {
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
