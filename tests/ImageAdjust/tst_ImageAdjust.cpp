#include <QtTest>

#include <QImage>

#include "ImageAdjust.h"

using ImageAdjust::Adjustments;
using ImageAdjust::ToneLut;
using ImageAdjust::applyTo;
using ImageAdjust::buildColorMatrix;
using ImageAdjust::buildToneLut;
using ImageAdjust::clamped;

namespace {

// A 1x1 image of one colour, in whichever format the test needs.
QImage pixel(QRgb colour, QImage::Format format = QImage::Format_ARGB32) {
    QImage img(1, 1, format);
    img.setPixel(0, 0, colour);
    return img;
}

QRgb only(const QImage& img) { return img.pixel(0, 0); }

// Fixed-point rounding of the colour matrix can land a channel one step off
// the exact real-arithmetic answer.
bool near(int actual, int expected, int tolerance = 1) {
    return qAbs(actual - expected) <= tolerance;
}

}  // namespace

// Coverage for the parameter -> pixels pipeline: the tone curve (gamma /
// contrast / brightness), the colour matrix (saturation / hue), range
// handling, and the format normalisation that keeps premultiplied sources
// from being silently mangled.
class tst_ImageAdjust : public QObject
{
    Q_OBJECT

private slots:

    // ---- identity ----

    void defaultsAreIdentity() {
        QVERIFY(Adjustments().isIdentity());
    }

    void identityReturnsSourceUnchanged() {
        const QImage src = pixel(qRgb(10, 120, 240));
        QCOMPARE(applyTo(src, Adjustments()), src);
    }

    void fullTurnOfHueIsIdentity() {
        Adjustments a;
        a.hue = 360;
        QVERIFY(a.isIdentity());
    }

    void identityToneLutIsFlat() {
        const ToneLut lut = buildToneLut(Adjustments());
        for (int v = 0; v < 256; ++v) QCOMPARE(int(lut[size_t(v)]), v);
    }

    void identityColorMatrixIsFlagged() {
        QVERIFY(buildColorMatrix(Adjustments()).identity);
        Adjustments a;
        a.saturation = 1;
        QVERIFY(!buildColorMatrix(a).identity);
    }

    // ---- ranges ----

    void outOfRangeValuesClamp() {
        Adjustments a;
        a.brightness = 5000;
        a.contrast   = -5000;
        a.saturation = 999;
        a.gamma      = 1000.0;
        const Adjustments c = clamped(a);
        QCOMPARE(c.brightness, 100);
        QCOMPARE(c.contrast, -100);
        QCOMPARE(c.saturation, 100);
        QCOMPARE(c.gamma, 10.0);
    }

    void hueWrapsRatherThanClamps() {
        Adjustments a;
        a.hue = 540;            // 360 + 180
        QCOMPARE(clamped(a).hue, 180);
        a.hue = -270;           // same as +90
        QCOMPARE(clamped(a).hue, 90);
        a.hue = 190;            // past the halfway point -> negative side
        QCOMPARE(clamped(a).hue, -170);
    }

    void extremesClampInsteadOfWrapping() {
        Adjustments a;
        a.brightness = 100;
        QCOMPARE(int(buildToneLut(a)[255]), 255);   // not 0 from an overflow
        a.brightness = -100;
        QCOMPARE(int(buildToneLut(a)[0]), 0);
    }

    // ---- tone curve: brightness ----

    void brightnessShiftsAndIsMonotonic() {
        Adjustments up;
        up.brightness = 50;
        const ToneLut lit = buildToneLut(up);
        QVERIFY(int(lit[128]) > 128);

        Adjustments down;
        down.brightness = -50;
        QVERIFY(int(buildToneLut(down)[128]) < 128);

        for (int v = 1; v < 256; ++v) {
            QVERIFY(lit[size_t(v)] >= lit[size_t(v - 1)]);
        }
    }

    // ---- tone curve: contrast ----

    void contrastPivotsAboutMidGrey() {
        Adjustments a;
        a.contrast = 60;
        const ToneLut lut = buildToneLut(a);
        QCOMPARE(int(lut[128]), 128);        // the pivot is a fixed point
        QVERIFY(int(lut[64]) < 64);          // darks pushed down
        QVERIFY(int(lut[192]) > 192);        // lights pushed up
    }

    void negativeContrastCompressesTowardMidGrey() {
        Adjustments a;
        a.contrast = -60;
        const ToneLut lut = buildToneLut(a);
        QVERIFY(int(lut[32]) > 32);
        QVERIFY(int(lut[224]) < 224);
    }

    // ---- tone curve: gamma ----

    void gammaAboveOneBrightensBelowOneDarkens() {
        Adjustments up;
        up.gamma = 2.2;
        QVERIFY(int(buildToneLut(up)[128]) > 128);

        Adjustments down;
        down.gamma = 0.45;
        QVERIFY(int(buildToneLut(down)[128]) < 128);
    }

    void gammaPreservesEndpoints() {
        Adjustments a;
        a.gamma = 2.2;
        const ToneLut lut = buildToneLut(a);
        QCOMPARE(int(lut[0]), 0);
        QCOMPARE(int(lut[255]), 255);
    }

    // ---- colour matrix: saturation ----

    void fullDesaturationYieldsGrey() {
        Adjustments a;
        a.saturation = -100;
        const QRgb out = only(applyTo(pixel(qRgb(255, 0, 0)), a));
        QCOMPARE(qRed(out), qGreen(out));
        QCOMPARE(qGreen(out), qBlue(out));
        // BT.709 luma of pure red: 0.213 * 255.
        QVERIFY(near(qRed(out), 54));
    }

    void saturationBoostPushesChannelsApart() {
        Adjustments a;
        a.saturation = 100;
        const QRgb src = qRgb(180, 120, 120);
        const QRgb out = only(applyTo(pixel(src), a));
        QVERIFY(qRed(out) > qRed(src));
        QVERIFY(qBlue(out) < qBlue(src));
    }

    void neutralGreySurvivesSaturation() {
        // Every row of the saturate matrix sums to 1, so grey is a fixed point
        // at any strength - if this drifts, the coefficients are wrong.
        const int strengths[] = { -100, -40, 40, 100 };
        for (int s : strengths) {
            Adjustments a;
            a.saturation = s;
            const QRgb out = only(applyTo(pixel(qRgb(128, 128, 128)), a));
            QVERIFY2(near(qRed(out), 128) && near(qGreen(out), 128)
                         && near(qBlue(out), 128),
                     qPrintable(QStringLiteral("saturation %1 moved grey").arg(s)));
        }
    }

    // ---- colour matrix: hue ----

    void neutralGreySurvivesHueRotation() {
        const int angles[] = { 30, 90, 180, -120 };
        for (int h : angles) {
            Adjustments a;
            a.hue = h;
            const QRgb out = only(applyTo(pixel(qRgb(128, 128, 128)), a));
            QVERIFY2(near(qRed(out), 128) && near(qGreen(out), 128)
                         && near(qBlue(out), 128),
                     qPrintable(QStringLiteral("hue %1 moved grey").arg(h)));
        }
    }

    void hueRotationMovesColour() {
        Adjustments a;
        a.hue = 120;
        const QRgb src = qRgb(255, 0, 0);
        const QRgb out = only(applyTo(pixel(src), a));
        QVERIFY(qGreen(out) > qGreen(src));   // red swings toward green
    }

    void hueRotationRoundTripsAtFullTurn() {
        Adjustments a;
        a.hue = 360;                          // identity, so no drift at all
        const QImage src = pixel(qRgb(200, 90, 40));
        QCOMPARE(only(applyTo(src, a)), only(src));
    }

    // ---- stage ordering ----

    void toneCurveRunsBeforeColourMatrix() {
        // Desaturating first would hand the tone curve a neutral 128,128,128
        // and the result would be neutral too; the documented order tones the
        // channels first, so the grey reflects the brightened red.
        Adjustments a;
        a.saturation = -100;
        a.brightness = 40;
        const QRgb out = only(applyTo(pixel(qRgb(255, 0, 0)), a));
        const ToneLut lut = buildToneLut(a);
        const int expected = qRound(0.213 * lut[255] + 0.715 * lut[0] + 0.072 * lut[0]);
        QVERIFY(near(qRed(out), expected));
    }

    // ---- image plumbing ----

    void alphaIsPreserved() {
        QImage src(1, 1, QImage::Format_ARGB32);
        src.setPixel(0, 0, qRgba(200, 100, 50, 77));
        Adjustments a;
        a.brightness = 30;
        a.saturation = 50;
        QCOMPARE(qAlpha(only(applyTo(src, a))), 77);
    }

    void premultipliedSourceIsUnpremultipliedFirst() {
        // The trap: in ARGB32_Premultiplied the stored channels are already
        // scaled by alpha, so running the curve over them directly is wrong.
        // Half-transparent mid-grey must adjust to the same colour as the
        // straight-alpha original.
        QImage straight(1, 1, QImage::Format_ARGB32);
        straight.setPixel(0, 0, qRgba(128, 128, 128, 128));
        const QImage premul =
            straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(premul.format(), QImage::Format_ARGB32_Premultiplied);

        Adjustments a;
        a.brightness = 40;
        const QRgb fromStraight = only(applyTo(straight, a));
        const QRgb fromPremul   = only(applyTo(premul, a));

        QVERIFY(near(qRed(fromPremul), qRed(fromStraight), 2));
        QVERIFY(near(qGreen(fromPremul), qGreen(fromStraight), 2));
        QVERIFY(near(qBlue(fromPremul), qBlue(fromStraight), 2));
    }

    void eightBitSourcesAreNormalisedToRgb() {
        QImage grey(1, 1, QImage::Format_Grayscale8);
        // fill(), not setPixel(): on an 8-bit format setPixel takes a colour
        // table *index*, and Grayscale8 has no colour table, so it rejects the
        // value with a warning and leaves the buffer uninitialised.
        grey.fill(100);
        Adjustments a;
        a.brightness = 30;
        const QImage out = applyTo(grey, a);
        QCOMPARE(out.format(), QImage::Format_RGB32);
        QVERIFY(qRed(only(out)) > 100);
    }

    void opaqueSourceStaysOpaque() {
        Adjustments a;
        a.contrast = 25;
        const QImage out = applyTo(pixel(qRgb(90, 90, 90), QImage::Format_RGB32), a);
        QCOMPARE(out.format(), QImage::Format_RGB32);
        QCOMPARE(qAlpha(only(out)), 255);
    }

    void geometryIsUntouched() {
        QImage src(7, 3, QImage::Format_ARGB32);
        src.fill(qRgb(50, 60, 70));
        Adjustments a;
        a.saturation = 25;
        const QImage out = applyTo(src, a);
        QCOMPARE(out.size(), QSize(7, 3));
    }

    void sourceIsNotMutated() {
        // applyTo must never write through to the caller's image - the whole
        // parameter model depends on the pristine source surviving.
        QImage src = pixel(qRgb(10, 20, 30));
        const QRgb before = only(src);
        Adjustments a;
        a.brightness = 60;
        a.saturation = -50;
        (void)applyTo(src, a);
        QCOMPARE(only(src), before);
    }

    void nullImageIsHandled() {
        Adjustments a;
        a.brightness = 50;
        QVERIFY(applyTo(QImage(), a).isNull());
    }
};

QTEST_GUILESS_MAIN(tst_ImageAdjust)
#include "tst_ImageAdjust.moc"
