#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QtTest>

#include "CopyFileTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstCopyFileTask : public QObject {
    Q_OBJECT

private slots:
    void happy_small_file();
    void happy_multi_mb_emits_progress_and_completes();
    void source_missing_fails();
    void dest_parent_is_created();
    void cancel_after_pause_aborts_and_removes_partial();
    void pause_then_resume_completes_with_correct_bytes();
    void conflict_skip_leaves_dest_untouched();
    void conflict_overwrite_replaces_dest();
    void conflict_rename_creates_uniquified_copy();
    void stop_while_awaiting_answer_aborts_without_writing();
};

namespace {

// Build a one-task group around `task`. Convenience for the per-test
// boilerplate — the manager takes ownership when enqueueGroup is called.
TaskGroup* makeGroup(CopyFileTask*& outTask,
                     const QString& src, const QString& dst) {
    auto* group = new TaskGroup(QStringLiteral("Copy"));
    outTask = new CopyFileTask(src, dst, group);
    group->addTask(outTask);
    return group;
}

} // namespace

// ---------------------------------------------------------------------------

void TstCopyFileTask::happy_small_file() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 1024);

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);
    QSignalSpy abortedSpy(task, &Task::aborted);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(abortedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(QFile::exists(dst));
    QVERIFY(TestHelpers::filesEqual(src, dst));
}

void TstCopyFileTask::happy_multi_mb_emits_progress_and_completes() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 4 * 1024 * 1024);   // 64 KB chunks → ~64 iterations

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy progressSpy(task, &Task::progress);
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished(15000));

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(progressSpy.count() > 2,
        qPrintable(QStringLiteral("expected multiple progress emissions, got %1")
            .arg(progressSpy.count())));

    int last = -1;
    for (const QList<QVariant>& args : progressSpy) {
        const int pct = args.value(1).toInt();
        QVERIFY2(pct >= last, "progress went backwards");
        QVERIFY(pct >= 0 && pct <= 100);
        last = pct;
    }
    QCOMPARE(last, 100);
    QVERIFY(TestHelpers::filesEqual(src, dst));
}

void TstCopyFileTask::source_missing_fails() {
    TaskTestFixture f;
    const QString src = f.path("does-not-exist.bin");
    const QString dst = f.path("dst.bin");

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy failedSpy(task, &Task::failed);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!failedSpy.first().value(1).toString().isEmpty());
    QCOMPARE(f.lastStateOf(id), int(Task::Failed));
    QVERIFY(!QFile::exists(dst));
}

void TstCopyFileTask::dest_parent_is_created() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("nested/sub/dir/dst.bin");
    TestHelpers::writeBytes(src, 256);

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(QFileInfo(f.path("nested/sub/dir")).isDir());
    QVERIFY(TestHelpers::filesEqual(src, dst));
}

void TstCopyFileTask::cancel_after_pause_aborts_and_removes_partial() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    // Big enough that the chunk loop will be running by the time pause
    // gets observed; pause-then-stop guarantees the cancel hits in the
    // middle of run() rather than racing the dispatch.
    TestHelpers::writeBytes(src, 16 * 1024 * 1024);

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy abortedSpy(task, &Task::aborted);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    // Wait until the run loop has actually started executing — paused
    // means checkPauseStop saw _pauseRequested and entered the wait CV.
    f.mgr.pauseTask(id);
    QTRY_COMPARE_WITH_TIMEOUT(task->state(), Task::Paused, 5000);

    f.mgr.stopTask(id);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(abortedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Aborted));
    QVERIFY2(!QFile::exists(dst), "partial dest should have been cleaned up");
}

void TstCopyFileTask::pause_then_resume_completes_with_correct_bytes() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 8 * 1024 * 1024);

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy abortedSpy(task, &Task::aborted);
    QSignalSpy progressSpy(task, &Task::progress);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    f.mgr.pauseTask(id);
    QTRY_COMPARE_WITH_TIMEOUT(task->state(), Task::Paused, 5000);

    // While paused, no further progress should arrive. Sample after a
    // short event-loop spin and assert the count didn't grow.
    const int frozen = progressSpy.count();
    QTest::qWait(200);
    QCOMPARE(progressSpy.count(), frozen);

    f.mgr.resumeTask(id);
    QVERIFY(f.waitForGroupFinished(15000));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(abortedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(TestHelpers::filesEqual(src, dst));
}

void TstCopyFileTask::conflict_skip_leaves_dest_untouched() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 1024);
    TestHelpers::writeBytes(dst, 256);
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    QCOMPARE(askedId, id);
    QCOMPARE(kind, int(Task::DestinationExists));
    f.mgr.provideAnswer(id, kind, int(Task::Skip), /*applyToGroup=*/false);

    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(f.lastStateOf(id), int(Task::Skipped));
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
}

void TstCopyFileTask::conflict_overwrite_replaces_dest() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 2048);
    TestHelpers::writeBytes(dst, 64);

    CopyFileTask* task = nullptr;
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
    QVERIFY(TestHelpers::filesEqual(src, dst));
}

void TstCopyFileTask::conflict_rename_creates_uniquified_copy() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("foo.bin");
    const QString uniquified = f.path("foo (1).bin");
    TestHelpers::writeBytes(src, 2048);
    TestHelpers::writeBytes(dst, 64);
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    CopyFileTask* task = nullptr;
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
    // Original dst untouched; new uniquified copy holds the source bytes.
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
    QVERIFY(QFile::exists(uniquified));
    QVERIFY(TestHelpers::filesEqual(src, uniquified));
}

void TstCopyFileTask::stop_while_awaiting_answer_aborts_without_writing() {
    TaskTestFixture f;
    const QString src = f.path("src.bin");
    const QString dst = f.path("dst.bin");
    TestHelpers::writeBytes(src, 1024);
    TestHelpers::writeBytes(dst, 64);
    QFile dstFile(dst);
    QVERIFY(dstFile.open(QIODevice::ReadOnly));
    const QByteArray dstBefore = dstFile.readAll();
    dstFile.close();

    CopyFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, src, dst);
    QSignalSpy abortedSpy(task, &Task::aborted);
    QSignalSpy finishedSpy(task, &Task::finished);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    // Don't provide an answer — stop instead. The answer-cv unblocks on
    // stop, run() bails on isStopRequested, execute emits aborted.
    f.mgr.stopTask(id);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(abortedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Aborted));
    QFile after(dst);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), dstBefore);
}

QTEST_GUILESS_MAIN(TstCopyFileTask)
#include "tst_CopyFileTask.moc"
