#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include "MoveFileTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstMoveFileTask : public QObject {
    Q_OBJECT

private slots:
    void same_volume_rename_completes();
    void conflict_skip_leaves_both_untouched();
    void conflict_overwrite_replaces_dest_and_removes_source();
    void conflict_rename_creates_uniquified_and_removes_source();
    void cross_volume_falls_back_to_copy_and_delete();
};

namespace {

TaskGroup* makeGroup(MoveFileTask*& outTask,
                     const QString& src, const QString& dst) {
    auto* group = new TaskGroup(QStringLiteral("Move"));
    outTask = new MoveFileTask(src, dst, group);
    group->addTask(outTask);
    return group;
}

// Locate a writable temp dir on a different volume than the primary tmp
// path, if one exists. Returns an empty string when no such volume is
// present (so the cross-volume test can QSKIP cleanly).
QString findOtherVolumeTempDir(const QString& primaryPath) {
    const QString primaryRoot =
        QStorageInfo(primaryPath).rootPath();
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        if (!vol.isValid() || !vol.isReady() || vol.isReadOnly()) continue;
        if (vol.rootPath() == primaryRoot) continue;
        // Try creating a temp dir at the volume root. Some volumes refuse
        // (no permissions, removable media) — just skip those.
        const QString candidate = vol.rootPath();
        QTemporaryDir probe(QDir(candidate).filePath("pixee-test-XXXXXX"));
        if (probe.isValid()) {
            // Hand back a path under that volume; let the caller create
            // its own dir there.
            return candidate;
        }
    }
    return {};
}

}

// ---------------------------------------------------------------------------

void TstMoveFileTask::same_volume_rename_completes() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 4096);
    QFile s(src);
    QVERIFY(s.open(QIODevice::ReadOnly));
    const QByteArray srcBytes = s.readAll();
    s.close();

    MoveFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy abortedSpy(task, &Task::aborted);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(abortedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(!QFile::exists(src));
    QVERIFY(QFile::exists(dst));

    QFile d(dst);
    QVERIFY(d.open(QIODevice::ReadOnly));
    QCOMPARE(d.readAll(), srcBytes);
}

void TstMoveFileTask::conflict_skip_leaves_both_untouched() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 1024);
    TestHelpers::writeBytes(dst, 256);

    QByteArray srcBefore, dstBefore;
    {
        QFile a(src), b(dst);
        QVERIFY(a.open(QIODevice::ReadOnly));
        QVERIFY(b.open(QIODevice::ReadOnly));
        srcBefore = a.readAll();
        dstBefore = b.readAll();
    }

    MoveFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    QCOMPARE(askedId, id);
    QCOMPARE(kind, int(Task::DestinationExists));
    f.mgr.provideAnswer(id, kind, int(Task::Skip), false);

    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Skipped));
    // Source kept (Skip is "do nothing"), dst untouched.
    QFile a(src), b(dst);
    QVERIFY(a.open(QIODevice::ReadOnly));
    QVERIFY(b.open(QIODevice::ReadOnly));
    QCOMPARE(a.readAll(), srcBefore);
    QCOMPARE(b.readAll(), dstBefore);
}

void TstMoveFileTask::conflict_overwrite_replaces_dest_and_removes_source() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 2048);
    TestHelpers::writeBytes(dst, 64);

    QByteArray srcBytes;
    {
        QFile a(src);
        QVERIFY(a.open(QIODevice::ReadOnly));
        srcBytes = a.readAll();
    }

    MoveFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(id, kind, int(Task::Overwrite), false);

    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(!QFile::exists(src));
    QVERIFY(QFile::exists(dst));

    QFile d(dst);
    QVERIFY(d.open(QIODevice::ReadOnly));
    QCOMPARE(d.readAll(), srcBytes);
}

void TstMoveFileTask::conflict_rename_creates_uniquified_and_removes_source() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("foo.bin");
    const QString uniquified = f.path("foo (1).bin");
    TestHelpers::writeBytes(src, 2048);
    TestHelpers::writeBytes(dst, 64);

    QByteArray srcBytes, dstBefore;
    {
        QFile a(src), b(dst);
        QVERIFY(a.open(QIODevice::ReadOnly));
        QVERIFY(b.open(QIODevice::ReadOnly));
        srcBytes = a.readAll();
        dstBefore = b.readAll();
    }

    MoveFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(id, kind, int(Task::Rename), false);

    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(!QFile::exists(src));
    // Original dst untouched; uniquified holds the source bytes.
    QFile d(dst);
    QVERIFY(d.open(QIODevice::ReadOnly));
    QCOMPARE(d.readAll(), dstBefore);
    QFile u(uniquified);
    QVERIFY(u.open(QIODevice::ReadOnly));
    QCOMPARE(u.readAll(), srcBytes);
}

void TstMoveFileTask::cross_volume_falls_back_to_copy_and_delete() {
    TaskTestFixture f;
    const QString other = findOtherVolumeTempDir(f.path());
    if (other.isEmpty()) {
        QSKIP("no second writable volume available — cross-volume path not exercised");
    }
    QTemporaryDir otherTmp(QDir(other).filePath("pixee-cross-XXXXXX"));
    QVERIFY(otherTmp.isValid());

    const QString src = f.path("src.bin");
    const QString dst = QDir(otherTmp.path()).filePath("dst.bin");
    TestHelpers::writeBytes(src, 64 * 1024);
    QByteArray srcBytes;
    {
        QFile a(src);
        QVERIFY(a.open(QIODevice::ReadOnly));
        srcBytes = a.readAll();
    }

    MoveFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished(15000));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(!QFile::exists(src));
    QVERIFY(QFile::exists(dst));

    QFile d(dst);
    QVERIFY(d.open(QIODevice::ReadOnly));
    QCOMPARE(d.readAll(), srcBytes);
}

QTEST_GUILESS_MAIN(TstMoveFileTask)
#include "tst_MoveFileTask.moc"
