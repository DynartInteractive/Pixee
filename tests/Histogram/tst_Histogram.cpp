#include <QtTest>

#include <QImage>

#include "Histogram.h"

using Histogram::Data;
using Histogram::compute;

namespace {

QImage solid(int w, int h, QRgb colour,
             QImage::Format format = QImage::Format_ARGB32) {
    QImage img(w, h, format);
    img.fill(colour);
    return img;
}

// Total across all 256 bins of one channel.
quint32 sum(const Histogram::Bins& bins) {
    quint32 total = 0;
    for (quint32 v : bins) total += v;
    return total;
}

}  // namespace

// Coverage for the binning: totals, clipping rules, the luma weights, the
// subsampling ceiling, and the format normalisation shared with ImageAdjust.
class tst_Histogram : public QObject
{
    Q_OBJECT

private slots:

    // ---- empty / degenerate ----

    void nullImageIsEmpty() {
        const Data d = compute(QImage());
        QVERIFY(d.isEmpty());
        QCOMPARE(d.sampled, 0u);
        QCOMPARE(d.peak, 0u);
    }

    // ---- totals ----

    void everyPixelIsCountedOnce() {
        const Data d = compute(solid(40, 30, qRgb(10, 20, 30)));
        QCOMPARE(d.sampled, 1200u);
        QCOMPARE(sum(d.red), 1200u);
        QCOMPARE(sum(d.green), 1200u);
        QCOMPARE(sum(d.blue), 1200u);
        QCOMPARE(sum(d.luma), 1200u);
    }

    void solidColourLandsInOneBinPerChannel() {
        const Data d = compute(solid(8, 8, qRgb(10, 20, 30)));
        QCOMPARE(d.red[10], 64u);
        QCOMPARE(d.green[20], 64u);
        QCOMPARE(d.blue[30], 64u);
        QCOMPARE(d.peak, 64u);
    }

    // ---- clipping ----

    void pureBlackIsShadowClipped() {
        const Data d = compute(solid(10, 10, qRgb(0, 0, 0)));
        QCOMPARE(d.clippedShadow, 100u);
        QCOMPARE(d.clippedHighlight, 0u);
    }

    void pureWhiteIsHighlightClipped() {
        const Data d = compute(solid(10, 10, qRgb(255, 255, 255)));
        QCOMPARE(d.clippedHighlight, 100u);
        QCOMPARE(d.clippedShadow, 0u);
    }

    void oneBlownChannelCountsAsHighlightClipped() {
        // Saturated red is not white, but the red channel has no headroom left
        // - that is the thing worth warning about.
        const Data d = compute(solid(10, 10, qRgb(255, 20, 20)));
        QCOMPARE(d.clippedHighlight, 100u);
    }

    void oneDarkChannelIsNotShadowClipped() {
        // Shadow clipping needs *all* channels at zero; a pixel with detail in
        // green still has detail.
        const Data d = compute(solid(10, 10, qRgb(0, 40, 0)));
        QCOMPARE(d.clippedShadow, 0u);
    }

    void midToneIsNotClippedEitherWay() {
        const Data d = compute(solid(10, 10, qRgb(128, 128, 128)));
        QCOMPARE(d.clippedShadow, 0u);
        QCOMPARE(d.clippedHighlight, 0u);
    }

    // ---- luma ----

    void lumaUsesBt709Weights() {
        // Same weights as ImageAdjust's colour matrix: red is ~0.213.
        const Data d = compute(solid(4, 4, qRgb(255, 0, 0)));
        int bin = -1;
        for (int i = 0; i < 256; ++i) {
            if (d.luma[size_t(i)] > 0) { bin = i; break; }
        }
        QVERIFY2(qAbs(bin - 54) <= 1, qPrintable(QStringLiteral("luma bin %1").arg(bin)));
    }

    void greyMapsLumaToItsOwnValue() {
        const Data d = compute(solid(4, 4, qRgb(200, 200, 200)));
        QVERIFY(d.luma[200] > 0 || d.luma[199] > 0 || d.luma[201] > 0);
    }

    // ---- distribution ----

    void gradientSpreadsAcrossBins() {
        QImage img(256, 4, QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < 256; ++x) img.setPixel(x, y, qRgb(x, x, x));
        }
        const Data d = compute(img);
        QCOMPARE(d.sampled, 1024u);
        for (int i = 0; i < 256; ++i) {
            QVERIFY2(d.red[size_t(i)] == 4u,
                     qPrintable(QStringLiteral("bin %1 = %2")
                                    .arg(i).arg(d.red[size_t(i)])));
        }
        QCOMPARE(d.peak, 4u);
    }

    // ---- subsampling ----

    void largeImageIsSubsampledUnderTheCeiling() {
        const Data d = compute(solid(1000, 1000, qRgb(90, 90, 90)), 10000);
        QVERIFY2(d.sampled <= 10000u,
                 qPrintable(QStringLiteral("sampled %1").arg(d.sampled)));
        QVERIFY(d.sampled > 0u);
        // The shape must survive: a solid image is still one bin.
        QCOMPARE(d.red[90], d.sampled);
    }

    void zeroCeilingCountsEveryPixel() {
        const Data d = compute(solid(300, 300, qRgb(1, 2, 3)), 0);
        QCOMPARE(d.sampled, 90000u);
    }

    void smallImageIsNotSubsampled() {
        const Data d = compute(solid(50, 50, qRgb(5, 5, 5)), 10000);
        QCOMPARE(d.sampled, 2500u);
    }

    // ---- format normalisation ----

    void premultipliedSourceMatchesStraightAlpha() {
        QImage straight(8, 8, QImage::Format_ARGB32);
        straight.fill(qRgba(128, 128, 128, 128));
        const QImage premul =
            straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        const Data a = compute(straight);
        const Data b = compute(premul);
        // Within a rounding step of each other, not scaled by alpha.
        int binA = -1, binB = -1;
        for (int i = 0; i < 256; ++i) {
            if (binA < 0 && a.red[size_t(i)] > 0) binA = i;
            if (binB < 0 && b.red[size_t(i)] > 0) binB = i;
        }
        QVERIFY2(qAbs(binA - binB) <= 1,
                 qPrintable(QStringLiteral("straight %1 vs premultiplied %2")
                                .arg(binA).arg(binB)));
    }

    void grayscaleSourceIsHandled() {
        QImage grey(8, 8, QImage::Format_Grayscale8);
        grey.fill(77);   // fill(), not setPixel() - see the CLAUDE.md gotcha
        const Data d = compute(grey);
        QCOMPARE(d.sampled, 64u);
        QCOMPARE(d.red[77], 64u);
    }
};

QTEST_GUILESS_MAIN(tst_Histogram)
#include "tst_Histogram.moc"
