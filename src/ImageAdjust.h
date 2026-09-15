#ifndef IMAGEADJUST_H
#define IMAGEADJUST_H

#include <QImage>

#include <array>

// Pure (GUI-free, filesystem-free) colour-adjustment maths behind the viewer's
// live Adjust panel. Kept separate from any widget so it can be unit tested
// with QtCore/QtGui alone.
//
// -- Why parameters, not baked pixels --------------------------------------
// The viewer's existing edits (rotate / flip / crop) bake straight into a
// buffer, which is right for a discrete one-shot transform. Sliders are not
// one-shot: re-applying "+5 brightness" to an already-brightened buffer on
// every mouse-move compounds the change and destroys data, and dragging back
// to zero would not restore the original. So an Adjustments value is a
// description of a transform, never a result, and applyTo() always recomputes
// from the pristine source. That single rule is what makes reset, before/after
// preview, and copying a look between images fall out for free.
namespace ImageAdjust {

// The full adjustment state. All-defaults is the identity transform.
//
// The pipeline applies these in a fixed, documented order:
//
//   1. tone curve, per channel, identical for R/G/B:  gamma -> contrast -> brightness
//   2. colour matrix, cross-channel:                  saturation -> hue
//
// Brightness deliberately comes *after* contrast so it stays a clean final
// offset: applied before, every brightness nudge would be multiplied by the
// contrast factor and the slider's feel would change depending on where
// contrast happened to be set.
//
// Deferred on purpose (each wants its own model, and none of them changes this
// shape): exposure, white-balance temperature/tint, per-channel levels, curves.
struct Adjustments {
    int    brightness = 0;    // -100 .. +100, 0 = unchanged
    int    contrast   = 0;    // -100 .. +100, 0 = unchanged
    int    saturation = 0;    // -100 (greyscale) .. 0 (unchanged) .. +100 (2x)
    int    hue        = 0;    // degrees, wraps; 0 = unchanged
    double gamma      = 1.0;  // 0.1 .. 10.0, 1.0 = unchanged; >1 brightens

    // True when applying this would be a no-op, so callers can skip the whole
    // pipeline (and the viewer can tell "edited" from "clean").
    bool isIdentity() const;

    bool operator==(const Adjustments& other) const;
    bool operator!=(const Adjustments& other) const { return !(*this == other); }
};

// `a` with every field forced into its supported range. Hue wraps rather than
// clamping (it is an angle), normalised to -180 .. +180.
Adjustments clamped(const Adjustments& a);

// -- Stage 1: the tone curve ----------------------------------------------
// gamma / contrast / brightness are per-channel functions of one 8-bit input,
// so they collapse into a 256-entry table built once per parameter change and
// read once per pixel. Exposed for testing.
using ToneLut = std::array<uchar, 256>;
ToneLut buildToneLut(const Adjustments& a);

// -- Stage 2: the colour matrix -------------------------------------------
// Saturation and hue are cross-channel, so no 1D table can express them - but
// both are linear, which means both are 3x3 matrices and the two multiply into
// one. Coefficients follow the W3C filter-effects definitions (feColorMatrix
// `saturate` / `hueRotate`, BT.709 luma weights), so the result matches what
// users already expect from every other tool.
//
// Stored as fixed point (`kShift` fractional bits) to keep the inner loop on
// integers. Row-major, 9 entries. `identity` is tracked explicitly rather than
// compared against the identity matrix, because rounding the coefficients into
// fixed point does not land exactly on it.
struct ColorMatrix {
    static constexpr int kShift = 16;
    qint32 m[9] = { 1 << kShift, 0, 0, 0, 1 << kShift, 0, 0, 0, 1 << kShift };
    bool identity = true;
};
ColorMatrix buildColorMatrix(const Adjustments& a);

// The adjusted image. Returns `src` untouched (a cheap implicitly-shared copy)
// when `a` is the identity, so the caller can apply unconditionally.
//
// Alpha is carried through unmodified. The result is always 32-bit: ARGB32 when
// the source had alpha, else RGB32. That normalisation matters - see the note
// in the .cpp about premultiplied formats, which is a silent correctness trap
// rather than a performance one.
QImage applyTo(const QImage& src, const Adjustments& a);

}  // namespace ImageAdjust

#endif // IMAGEADJUST_H
