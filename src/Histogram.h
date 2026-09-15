#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QImage>
#include <QtGlobal>

#include <array>

// Pure (GUI-free) tone-distribution counting for the histogram dock. Kept
// separate from the panel that draws it so it can be unit tested with
// QtCore/QtGui alone, the same split as ImageAdjust and BatchRenamePlan.
namespace Histogram {

using Bins = std::array<quint32, 256>;

struct Data {
    Bins red{};
    Bins green{};
    Bins blue{};
    Bins luma{};

    // Pixels actually counted. Not the image's pixel count when the source was
    // subsampled - percentages must be taken against this, not against w*h.
    quint32 sampled = 0;

    // Tallest bin across all four channels; 0 for an empty result. The panel
    // scales against this, and it is the number a log scale tames.
    quint32 peak = 0;

    // Clipping, the reason to look at a histogram while dragging contrast.
    // Shadow = every channel at 0 (no detail left to recover); highlight =
    // any channel at 255 (that channel has blown, even if the pixel is not
    // white). The two use different rules on purpose.
    quint32 clippedShadow = 0;
    quint32 clippedHighlight = 0;

    bool isEmpty() const { return sampled == 0; }
};

// Default sampling ceiling. A histogram is a distribution, and a systematic
// 1-in-N grid sample of a photograph has the same shape as the full count, so
// there is no reason to walk 24 million pixels to draw 256 bars.
constexpr int kDefaultMaxSamples = 1 << 20;   // ~1M

// Count `image` into per-channel bins. Subsamples on a regular grid when the
// image has more than `maxSamples` pixels; pass <= 0 to count every pixel.
// A null image yields an empty Data.
Data compute(const QImage& image, int maxSamples = kDefaultMaxSamples);

}  // namespace Histogram

#endif // HISTOGRAM_H
