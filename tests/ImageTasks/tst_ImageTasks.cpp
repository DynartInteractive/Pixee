// EXIF-orientation round-trip is intentionally NOT covered here — Qt
// doesn't expose a simple way to write a JPEG with an EXIF orientation
// tag, and hand-crafting one is more complex than the test would be
// worth. Manual exercise: feed the app a real phone photo with
// orientation != 1 and confirm the scaled output matches the displayed
// orientation, not the stored bytes.

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QtTest>

#include "ConvertFormatTask.h"
#include "ImageFormats.h"
#include "SaveImageTask.h"
#include "ScaleImageTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstImageTasks : public QObject {
    Q_OBJECT

private slots:
    void scale_downscales_landscape_preserving_aspect();
    void scale_does_not_upscale_smaller_than_target();
    void scale_skip_conflict_leaves_dest_untouched();
    void scale_writes_jfif_destination_as_jpeg();
    void scale_applies_jpeg_quality();
    void formats_isLossy_covers_every_quality_bearing_format();
    void convert_applies_webp_quality();
    void convert_png_to_jpg_produces_valid_jpeg();
    void convert_skip_conflict_leaves_dest_untouched();
    void save_writes_in_memory_image_to_disk();
    void save_null_image_fails();
    void save_overwrite_flag_bypasses_prompt();
    void save_skip_conflict_leaves_dest_untouched();
};

namespace {

ScaleImageTask* addScale(TaskGroup* group, const QString& src, const QString& dst,
                         int longest, int q = 92) {
    auto* t = new ScaleImageTask(src, dst, longest, q, group);
    group->addTask(t);
    return t;
}

ConvertFormatTask* addConvert(TaskGroup* group, const QString& src,
                              const QString& dst, const QByteArray& fmt,
                              int q = 92) {
    auto* t = new ConvertFormatTask(src, dst, fmt, q, group);
    group->addTask(t);
    return t;
}

SaveImageTask* addSave(TaskGroup* group, const QImage& img, const QString& dst,
                       const QByteArray& fmt, int q = 92,
                       bool overwrite = false) {
    auto* t = new SaveImageTask(img, dst, fmt, q, group, nullptr, overwrite);
    group->addTask(t);
    return t;
}

// TestHelpers::writeImage paints a flat fill, which JPEG compresses to
// near-identical sizes at any quality. Measuring quality needs
// high-frequency detail, so generate some deterministically.
void writeNoisyPng(const QString& path, int width, int height) {
    QImage img(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img.setPixel(x, y, qRgb((x * 37 + y * 17) % 256,
                                    (x * 53 + y * 91) % 256,
                                    (x * 11 + y * 71) % 256));
        }
    }
    QVERIFY(img.save(path, "png"));
}

}

// ---------------------------------------------------------------------------

void TstImageTasks::scale_downscales_landscape_preserving_aspect() {
    TaskTestFixture f;
    const QString src = f.path("big.png");
    const QString dst = f.path("scaled.png");
    TestHelpers::writeImage(src, 2000, 1000, "png");

    auto* group = new TaskGroup(QStringLiteral("Scale"));
    auto* task = addScale(group, src, dst, /*longest=*/1024);
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(QFile::exists(dst));
    QImageReader reader(dst);
    const QSize size = reader.size();
    QVERIFY(size.isValid());
    QCOMPARE(size.width(), 1024);
    QCOMPARE(size.height(), 512);    // aspect preserved 2:1
}

void TstImageTasks::scale_writes_jfif_destination_as_jpeg() {
    // .jfif holds plain JPEG bytes under a suffix no Qt plugin advertises.
    // Left to infer the format from the destination name, QImageWriter
    // fails the write outright with "Unsupported image format".
    TaskTestFixture f;
    const QString src = f.path("big.png");
    const QString dst = f.path("scaled.jfif");
    TestHelpers::writeImage(src, 800, 400, "png");

    auto* group = new TaskGroup(QStringLiteral("Scale"));
    auto* task = addScale(group, src, dst, /*longest=*/256);
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(QFile::exists(dst));
    // QImageReader sniffs content, so this proves real JPEG bytes landed
    // rather than the suffix merely having been accepted.
    QImageReader reader(dst);
    QCOMPARE(reader.format(), QByteArray("jpeg"));
    QCOMPARE(reader.size(), QSize(256, 128));
}

void TstImageTasks::scale_applies_jpeg_quality() {
    // Regression: the writer used to be built from the destination name
    // alone, and such a writer reports an empty format(), so the branch
    // guarding setQuality() never matched and the quality was ignored.
    TaskTestFixture f;
    const QString src = f.path("noisy.png");
    writeNoisyPng(src, 900, 600);
    const QString low = f.path("low.jpg");
    const QString high = f.path("high.jpg");

    auto* group = new TaskGroup(QStringLiteral("Scale"));
    addScale(group, src, low, /*longest=*/512, /*q=*/10);
    addScale(group, src, high, /*longest=*/512, /*q=*/95);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QVERIFY(QFile::exists(low));
    QVERIFY(QFile::exists(high));
    QVERIFY2(QFileInfo(low).size() < QFileInfo(high).size(),
             "quality 10 should encode smaller than quality 95");
}

void TstImageTasks::formats_isLossy_covers_every_quality_bearing_format() {
    // The single source of truth behind the Save As quality slider, the
    // setQuality() calls in the three image tasks, and the "re-saving costs
    // quality" warning on File → Save. It used to be four divergent inline
    // comparisons; this pins the set so they can't drift apart again.
    for (const QString& lossy : { QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("jfif"), QStringLiteral("webp"),
                                  QStringLiteral("jxl") }) {
        QVERIFY2(ImageFormats::isLossy(lossy), qPrintable(lossy));
        QVERIFY2(ImageFormats::isLossy(lossy.toUpper()), qPrintable(lossy));
    }
    for (const QString& lossless : { QStringLiteral("png"), QStringLiteral("gif"),
                                     QStringLiteral("bmp"), QStringLiteral("tiff"),
                                     QStringLiteral("ico"), QStringLiteral("") }) {
        QVERIFY2(!ImageFormats::isLossy(lossless), qPrintable(lossless));
    }
}

void TstImageTasks::convert_applies_webp_quality() {
    // Regression: ConvertFormatTask only called setQuality() for jpg/jpeg, so
    // the quality the user picked in SaveAsDialog — which does show the slider
    // for WebP — was dropped on the floor for every other lossy format.
    if (!QImageWriter::supportedImageFormats().contains("webp")) {
        QSKIP("no WebP writer in this Qt build");
    }

    TaskTestFixture f;
    const QString src = f.path("noisy.png");
    writeNoisyPng(src, 900, 600);
    const QString low = f.path("low.webp");
    const QString high = f.path("high.webp");

    auto* group = new TaskGroup(QStringLiteral("Convert"));
    addConvert(group, src, low, "webp", /*q=*/10);
    addConvert(group, src, high, "webp", /*q=*/95);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QVERIFY(QFile::exists(low));
    QVERIFY(QFile::exists(high));
    QVERIFY2(QFileInfo(low).size() < QFileInfo(high).size(),
             "quality 10 should encode smaller than quality 95");
}

void TstImageTasks::scale_does_not_upscale_smaller_than_target() {
    // Source already smaller than the longest-edge target — task should
    // re-encode at original size, not upscale (matches the > comparison
    // in ScaleImageTask::run).
    TaskTestFixture f;
    const QString src = f.path("small.png");
    const QString dst = f.path("scaled.png");
    TestHelpers::writeImage(src, 400, 200, "png");

    auto* group = new TaskGroup(QStringLiteral("Scale"));
    addScale(group, src, dst, /*longest=*/1024);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QImageReader reader(dst);
    const QSize size = reader.size();
    QCOMPARE(size, QSize(400, 200));
}

void TstImageTasks::scale_skip_conflict_leaves_dest_untouched() {
    TaskTestFixture f;
    const QString src = f.path("big.png");
    const QString dst = f.path("dst.png");
    TestHelpers::writeImage(src, 2000, 1000, "png");
    // Pre-existing dst with different content (different size).
    TestHelpers::writeImage(dst, 100, 100, "png");
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    auto* group = new TaskGroup(QStringLiteral("Scale"));
    auto* task = addScale(group, src, dst, 1024);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(id, kind, int(Task::Skip), false);
    QVERIFY(f.waitForGroupRemoved());

    QCOMPARE(f.lastStateOf(id), int(Task::Skipped));
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
}

void TstImageTasks::convert_png_to_jpg_produces_valid_jpeg() {
    TaskTestFixture f;
    const QString src = f.path("source.png");
    const QString dst = f.path("converted.jpg");
    TestHelpers::writeImage(src, 800, 600, "png");

    auto* group = new TaskGroup(QStringLiteral("Convert"));
    auto* task = addConvert(group, src, dst, "jpg");
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(finishedSpy.count(), 1);
    QImageReader reader(dst);
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    QCOMPARE(reader.format().toLower(), QByteArray("jpeg"));
    QCOMPARE(reader.size(), QSize(800, 600));
}

void TstImageTasks::convert_skip_conflict_leaves_dest_untouched() {
    TaskTestFixture f;
    const QString src = f.path("source.png");
    const QString dst = f.path("converted.jpg");
    TestHelpers::writeImage(src, 200, 200, "png");
    TestHelpers::writeImage(dst, 50, 50, "jpg");
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    auto* group = new TaskGroup(QStringLiteral("Convert"));
    auto* task = addConvert(group, src, dst, "jpg");
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(id, kind, int(Task::Skip), false);
    QVERIFY(f.waitForGroupRemoved());

    QCOMPARE(f.lastStateOf(id), int(Task::Skipped));
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
}

void TstImageTasks::save_writes_in_memory_image_to_disk() {
    // The core case: an image that exists only in memory (an edit) is written
    // out — no source file to decode.
    TaskTestFixture f;
    const QString dst = f.path("edited.png");
    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(Qt::red);

    auto* group = new TaskGroup(QStringLiteral("Save"));
    auto* task = addSave(group, img, dst, "png");
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(QFile::exists(dst));
    QImageReader reader(dst);
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    QCOMPARE(reader.size(), QSize(120, 80));
    QCOMPARE(reader.read().pixelColor(0, 0), QColor(Qt::red));
}

void TstImageTasks::save_null_image_fails() {
    TaskTestFixture f;
    const QString dst = f.path("nope.png");

    auto* group = new TaskGroup(QStringLiteral("Save"));
    auto* task = addSave(group, QImage(), dst, "png");
    const QUuid id = task->id();
    QSignalSpy failedSpy(task, &Task::failed);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Failed));
    QVERIFY(!QFile::exists(dst));
}

void TstImageTasks::save_overwrite_flag_bypasses_prompt() {
    // With overwriteExisting=true the task must NOT ask — it just replaces the
    // file. If it wrongly prompted, no answer is ever provided and the group
    // would hang, so waitForGroupRemoved timing out is the failure signal.
    TaskTestFixture f;
    const QString dst = f.path("target.png");
    TestHelpers::writeImage(dst, 40, 40, "png");   // pre-existing, different size

    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(Qt::blue);

    auto* group = new TaskGroup(QStringLiteral("Save"));
    auto* task = addSave(group, img, dst, "png", 92, /*overwrite=*/true);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved(10000));

    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QImageReader reader(dst);
    QCOMPARE(reader.size(), QSize(120, 80));   // new content won
}

void TstImageTasks::save_skip_conflict_leaves_dest_untouched() {
    // Default (overwriteExisting=false): existing dst triggers the prompt,
    // and Skip leaves the file byte-for-byte unchanged.
    TaskTestFixture f;
    const QString dst = f.path("target.png");
    TestHelpers::writeImage(dst, 40, 40, "png");
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(Qt::green);

    auto* group = new TaskGroup(QStringLiteral("Save"));
    auto* task = addSave(group, img, dst, "png");
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(id, kind, int(Task::Skip), false);
    QVERIFY(f.waitForGroupRemoved());

    QCOMPARE(f.lastStateOf(id), int(Task::Skipped));
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
}

QTEST_GUILESS_MAIN(TstImageTasks)
#include "tst_ImageTasks.moc"
