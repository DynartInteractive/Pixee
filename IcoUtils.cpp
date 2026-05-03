#include "IcoUtils.h"

#include <QImage>
#include <QImageReader>
#include <QString>

namespace IcoUtils {

QImage pickBestSubImage(QImageReader& reader, const QString& path) {
    if (!path.endsWith(QLatin1String(".ico"), Qt::CaseInsensitive)) {
        return QImage();
    }
    if (reader.imageCount() <= 1) {
        return QImage();
    }

    // ICO files contain a directory of sub-images. Qt's ICO handler reads
    // sizes from those directory entries, but the field is one unsigned
    // byte — `0x00` actually means 256 px, and PNG-encoded entries can lie
    // outright. Asking reader.size() per entry then picking the largest
    // misses the high-res entry whenever it's reported as 0×0.
    //
    // Workaround: actually decode every sub-image and compare the resulting
    // QImage sizes. N decodes for N entries, but the file is typically
    // already in an in-memory buffer so it's cheap.
    //
    // Most ICO files contain several entries at the same dimensions but
    // different bit depths (e.g. 48×48 in 256-color, 24-bit, and 32-bit
    // forms) — by convention the higher-quality versions come last in
    // the directory. Score by area first; on a tie, prefer higher
    // QImage::depth(), and `>=` lets later entries with the same depth
    // win too (covers the case where the handler normalises everything
    // to ARGB32 and depth-tiebreak alone wouldn't help).
    QImage best;
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
            best = candidate;
        }
    }
    return best;
}

}
