#include "Histogram.h"

namespace {

// BT.709 luma in fixed point, matching the weights ImageAdjust's colour matrix
// uses so the luma curve agrees with what the saturation slider pivots around.
// 55 + 183 + 18 == 256, so the shift cannot overflow 255.
inline int luma709(int r, int g, int b) {
    return (55 * r + 183 * g + 18 * b) >> 8;
}

}  // namespace

namespace Histogram {

Data compute(const QImage& image, int maxSamples) {
    Data data;
    if (image.isNull()) return data;

    // Same normalisation as ImageAdjust::applyTo, and for the same reason: the
    // decoder hands out whatever it likes, and premultiplied channels are
    // scaled by alpha, which would skew every bin on an image with
    // transparency.
    const QImage::Format target = image.hasAlphaChannel() ? QImage::Format_ARGB32
                                                          : QImage::Format_RGB32;
    const QImage src = image.format() == target ? image
                                                : image.convertToFormat(target);

    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) return data;

    // Grid step, chosen so (w/step) * (h/step) fits under the ceiling. Stepping
    // both axes keeps the sample spatially even; striding rows only would bias
    // an image with horizontal structure.
    int step = 1;
    if (maxSamples > 0) {
        while (qint64(w / step + 1) * qint64(h / step + 1) > qint64(maxSamples)) {
            ++step;
        }
    }

    const uchar* const base = src.constBits();
    const qsizetype stride = src.bytesPerLine();

    for (int y = 0; y < h; y += step) {
        const QRgb* const row = reinterpret_cast<const QRgb*>(base + y * stride);
        for (int x = 0; x < w; x += step) {
            const QRgb px = row[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);

            ++data.red[static_cast<size_t>(r)];
            ++data.green[static_cast<size_t>(g)];
            ++data.blue[static_cast<size_t>(b)];
            ++data.luma[static_cast<size_t>(luma709(r, g, b))];
            ++data.sampled;

            if (r == 0 && g == 0 && b == 0) ++data.clippedShadow;
            if (r == 255 || g == 255 || b == 255) ++data.clippedHighlight;
        }
    }

    for (size_t i = 0; i < 256; ++i) {
        data.peak = qMax(data.peak, data.red[i]);
        data.peak = qMax(data.peak, data.green[i]);
        data.peak = qMax(data.peak, data.blue[i]);
        data.peak = qMax(data.peak, data.luma[i]);
    }
    return data;
}

}  // namespace Histogram
