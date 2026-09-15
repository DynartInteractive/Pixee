#include "ImageAdjust.h"

#include <QtGlobal>

#include <cmath>

namespace {

// BT.709 luma weights, matching the W3C filter-effects colour matrices.
constexpr double kLumaR = 0.213;
constexpr double kLumaG = 0.715;
constexpr double kLumaB = 0.072;

// MSVC's <cmath> only defines M_PI when _USE_MATH_DEFINES is set before the
// include, so spell it out rather than depend on that.
constexpr double kPi = 3.14159265358979323846;

// Hue is an angle: wrap it into -180..+180 instead of clamping, so a slider
// that runs past the end comes out the other side.
int wrapHue(int degrees) {
    int h = ((degrees % 360) + 360) % 360;
    if (h > 180) h -= 360;
    return h;
}

// out = a * b, both row-major 3x3.
void matMul(const double a[9], const double b[9], double out[9]) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                           + a[r * 3 + 1] * b[1 * 3 + c]
                           + a[r * 3 + 2] * b[2 * 3 + c];
        }
    }
}

// Clamp a fixed-point accumulator into 0..255. Shifting only after the
// negative case is handled keeps this off implementation-defined ground: a
// right shift of a negative integer is not portably an arithmetic shift.
inline int clampShift(qint32 v) {
    if (v <= 0) return 0;
    v >>= ImageAdjust::ColorMatrix::kShift;
    return v > 255 ? 255 : static_cast<int>(v);
}

inline int clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

}  // namespace

namespace ImageAdjust {

bool Adjustments::isIdentity() const {
    return brightness == 0
        && contrast == 0
        && saturation == 0
        && wrapHue(hue) == 0
        && qFuzzyCompare(gamma, 1.0);
}

bool Adjustments::operator==(const Adjustments& other) const {
    return brightness == other.brightness
        && contrast == other.contrast
        && saturation == other.saturation
        && wrapHue(hue) == wrapHue(other.hue)
        && qFuzzyCompare(gamma, other.gamma);
}

Adjustments clamped(const Adjustments& a) {
    Adjustments out = a;
    out.brightness = qBound(-100, out.brightness, 100);
    out.contrast   = qBound(-100, out.contrast, 100);
    out.saturation = qBound(-100, out.saturation, 100);
    out.hue        = wrapHue(out.hue);
    out.gamma      = qBound(0.1, out.gamma, 10.0);
    return out;
}

ToneLut buildToneLut(const Adjustments& in) {
    const Adjustments a = clamped(in);

    // Classic contrast factor: -100..+100 maps onto the -255..+255 the formula
    // expects, and the curve pivots about mid-grey so 128 is a fixed point.
    const double c = a.contrast * 255.0 / 100.0;
    const double factor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));

    // +-100 brightness shifts mid-grey all the way to white / black.
    const double offset = a.brightness * 128.0 / 100.0;

    const bool doGamma = !qFuzzyCompare(a.gamma, 1.0);
    const double invGamma = 1.0 / a.gamma;

    ToneLut lut{};
    for (int v = 0; v < 256; ++v) {
        double x = v;
        if (doGamma) x = 255.0 * std::pow(x / 255.0, invGamma);
        x = factor * (x - 128.0) + 128.0;   // contrast, about mid-grey
        x += offset;                        // brightness, a clean final offset
        lut[static_cast<size_t>(v)] = static_cast<uchar>(clamp255(qRound(x)));
    }
    return lut;
}

ColorMatrix buildColorMatrix(const Adjustments& in) {
    const Adjustments a = clamped(in);

    ColorMatrix out;
    if (a.saturation == 0 && a.hue == 0) return out;  // identity, flag already set

    // feColorMatrix type="saturate": scales chroma about the luma axis.
    // s = 0 collapses to greyscale, 1 is unchanged, 2 doubles.
    const double s = 1.0 + a.saturation / 100.0;
    const double sat[9] = {
        kLumaR + (1.0 - kLumaR) * s, kLumaG - kLumaG * s,         kLumaB - kLumaB * s,
        kLumaR - kLumaR * s,         kLumaG + (1.0 - kLumaG) * s, kLumaB - kLumaB * s,
        kLumaR - kLumaR * s,         kLumaG - kLumaG * s,         kLumaB + (1.0 - kLumaB) * s,
    };

    // feColorMatrix type="hueRotate": rotates about the luma axis.
    const double rad = a.hue * kPi / 180.0;
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);
    const double hue[9] = {
        0.213 + cosA * 0.787 - sinA * 0.213,
        0.715 - cosA * 0.715 - sinA * 0.715,
        0.072 - cosA * 0.072 + sinA * 0.928,

        0.213 - cosA * 0.213 + sinA * 0.143,
        0.715 + cosA * 0.285 + sinA * 0.140,
        0.072 - cosA * 0.072 - sinA * 0.283,

        0.213 - cosA * 0.213 - sinA * 0.787,
        0.715 - cosA * 0.715 + sinA * 0.715,
        0.072 + cosA * 0.928 + sinA * 0.072,
    };

    // Saturation first, then hue. Every row of both matrices sums to 1, so the
    // product's rows do too - which is why neutral grey survives either one
    // untouched, an invariant the tests pin.
    double combined[9];
    matMul(hue, sat, combined);

    for (int i = 0; i < 9; ++i) {
        out.m[i] = static_cast<qint32>(qRound(combined[i] * (1 << ColorMatrix::kShift)));
    }
    out.identity = false;
    return out;
}

QImage applyTo(const QImage& src, const Adjustments& in) {
    const Adjustments a = clamped(in);
    if (src.isNull() || a.isIdentity()) return src;

    // Normalise to a 32-bit QRgb layout so the inner loop can assume one.
    //
    // This is not only about speed. Qt hands out whatever the decoder produced
    // - Indexed8 for GIF, Grayscale8, RGBA64 for 16-bit PNG, and very often
    // ARGB32_Premultiplied, which is its fast paint format. Premultiplied is
    // the trap: the stored channels are already scaled by alpha, so scaling
    // them again by a contrast factor is meaningless and quietly wrong on any
    // image with transparency. convertToFormat un-premultiplies for us.
    //
    // (>8 bits per channel is flattened to 8 here. That is fine for the live
    // preview this feeds; a future full-precision save path would want to keep
    // RGBA64 and widen the tables rather than change this shape.)
    const QImage::Format target = src.hasAlphaChannel() ? QImage::Format_ARGB32
                                                        : QImage::Format_RGB32;
    QImage img = src;
    if (img.format() != target) img = img.convertToFormat(target);

    const ToneLut lut = buildToneLut(a);
    const ColorMatrix cm = buildColorMatrix(a);

    // One detach up front, then plain pointer arithmetic - scanLine() would
    // re-check the refcount on every row, and pixel()/setPixel() are an order
    // of magnitude slower still.
    uchar* const base = img.bits();
    const qsizetype stride = img.bytesPerLine();
    const int w = img.width();
    const int h = img.height();

    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(base + y * stride);
        for (int x = 0; x < w; ++x) {
            const QRgb px = row[x];
            int r = lut[static_cast<size_t>(qRed(px))];
            int g = lut[static_cast<size_t>(qGreen(px))];
            int b = lut[static_cast<size_t>(qBlue(px))];

            if (!cm.identity) {
                const qint32 nr = cm.m[0] * r + cm.m[1] * g + cm.m[2] * b;
                const qint32 ng = cm.m[3] * r + cm.m[4] * g + cm.m[5] * b;
                const qint32 nb = cm.m[6] * r + cm.m[7] * g + cm.m[8] * b;
                r = clampShift(nr);
                g = clampShift(ng);
                b = clampShift(nb);
            }

            row[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
    return img;
}

}  // namespace ImageAdjust
