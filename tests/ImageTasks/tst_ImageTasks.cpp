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
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QtTest>

#include "ConvertFormatTask.h"
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
    void convert_png_to_jpg_produces_valid_jpeg();
    void convert_skip_conflict_leaves_dest_untouched();
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

QTEST_GUILESS_MAIN(TstImageTasks)
#include "tst_ImageTasks.moc"
