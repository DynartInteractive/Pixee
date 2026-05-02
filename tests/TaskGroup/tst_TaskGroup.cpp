#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <QtTest>

#include "CopyFileTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstTaskGroup : public QObject {
    Q_OBJECT

private slots:
    void sticky_overwrite_short_circuits_remaining_tasks_in_group();
    void sticky_answer_does_not_cross_groups();
    void stop_group_aborts_queued_tasks();
    void sequential_dispatch_within_a_group();
    void parallel_dispatch_across_groups();
    void path_touched_emits_for_completed_task();
    void shutdown_is_idempotent();
    void shutdown_mid_run_returns_promptly();
};

namespace {

CopyFileTask* addCopy(TaskGroup* group, const QString& src, const QString& dst) {
    auto* t = new CopyFileTask(src, dst, group);
    group->addTask(t);
    return t;
}

}

// ---------------------------------------------------------------------------

void TstTaskGroup::sticky_overwrite_short_circuits_remaining_tasks_in_group() {
    TaskTestFixture f;
    // Three sources + three pre-existing dests, all conflicting.
    for (int i = 0; i < 3; ++i) {
        TestHelpers::writeBytes(f.path(QString("src%1.bin").arg(i)), 1024);
        TestHelpers::writeBytes(f.path(QString("dst%1.bin").arg(i)), 64);
    }

    auto* group = new TaskGroup(QStringLiteral("Copy x3"));
    for (int i = 0; i < 3; ++i) {
        addCopy(group,
                f.path(QString("src%1.bin").arg(i)),
                f.path(QString("dst%1.bin").arg(i)));
    }
    f.mgr.enqueueGroup(group);

    // Only the first task should pose a question; answer with apply-to-group.
    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(askedId, kind, int(Task::Overwrite),
                        /*applyToGroup=*/true);

    QVERIFY(f.waitForGroupRemoved(10000));

    // No further questions in the queue (waitForQuestion popped the only one).
    QCOMPARE(f.questionSpy.count(), 0);

    // All three dests now byte-equal their source.
    for (int i = 0; i < 3; ++i) {
        QVERIFY(TestHelpers::filesEqual(
            f.path(QString("src%1.bin").arg(i)),
            f.path(QString("dst%1.bin").arg(i))));
    }
}

void TstTaskGroup::sticky_answer_does_not_cross_groups() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src1.bin"), 1024);
    TestHelpers::writeBytes(f.path("dst1.bin"), 64);
    TestHelpers::writeBytes(f.path("src2.bin"), 1024);
    TestHelpers::writeBytes(f.path("dst2.bin"), 64);

    // Group A — single conflicting copy. Answer Overwrite with apply-to-group.
    auto* groupA = new TaskGroup(QStringLiteral("A"));
    addCopy(groupA, f.path("src1.bin"), f.path("dst1.bin"));
    f.mgr.enqueueGroup(groupA);

    QUuid askedId; int kind = -1; QVariantMap ctx;
    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(askedId, kind, int(Task::Overwrite), true);
    QTRY_COMPARE_WITH_TIMEOUT(f.groupRemovedSpy.count(), 1, 5000);

    // Group B — different group, same conflict shape. Sticky from A must
    // NOT carry over: the new group has its own (empty) sticky map.
    auto* groupB = new TaskGroup(QStringLiteral("B"));
    addCopy(groupB, f.path("src2.bin"), f.path("dst2.bin"));
    f.mgr.enqueueGroup(groupB);

    QVERIFY(f.waitForQuestion(&askedId, &kind, &ctx));
    f.mgr.provideAnswer(askedId, kind, int(Task::Overwrite), false);
    QTRY_COMPARE_WITH_TIMEOUT(f.groupRemovedSpy.count(), 2, 5000);
}

void TstTaskGroup::stop_group_aborts_queued_tasks() {
    TaskTestFixture f;
    for (int i = 0; i < 3; ++i) {
        TestHelpers::writeBytes(f.path(QString("src%1.bin").arg(i)), 4 * 1024 * 1024);
    }

    auto* group = new TaskGroup(QStringLiteral("Three"));
    QVector<CopyFileTask*> tasks;
    for (int i = 0; i < 3; ++i) {
        tasks.append(addCopy(group,
                             f.path(QString("src%1.bin").arg(i)),
                             f.path(QString("dst%1.bin").arg(i))));
    }
    const QUuid groupId = group->id();

    f.mgr.enqueueGroup(group);

    // Pause t0 to deterministically catch it inside run() — the chunked
    // loop hits checkPauseStop on every 64 KB iteration, so pause is
    // observed before the file completes (vs. polling for Running, which
    // races on small files). When stopGroup fires, t0 is in Paused (the
    // CV wakes on stop), and t1/t2 are Queued (aborted via requestStop's
    // early CAS path).
    f.mgr.pauseTask(tasks[0]->id());
    QTRY_COMPARE_WITH_TIMEOUT(tasks[0]->state(), Task::Paused, 5000);
    f.mgr.stopGroup(groupId);

    QVERIFY(f.waitForGroupRemoved(10000));

    for (CopyFileTask* t : tasks) {
        QCOMPARE(f.lastStateOf(t->id()), int(Task::Aborted));
    }
}

void TstTaskGroup::sequential_dispatch_within_a_group() {
    TaskTestFixture f;                   // 2 workers
    TestHelpers::writeBytes(f.path("a.bin"), 4 * 1024 * 1024);
    TestHelpers::writeBytes(f.path("b.bin"), 4 * 1024 * 1024);

    auto* group = new TaskGroup(QStringLiteral("Seq"));
    auto* t1 = addCopy(group, f.path("a.bin"), f.path("a-out.bin"));
    auto* t2 = addCopy(group, f.path("b.bin"), f.path("b-out.bin"));
    f.mgr.enqueueGroup(group);

    // Pause t1 — works whether dispatch has started t1 yet or not, since
    // the pause flag is queued and consumed at t1's first checkPauseStop.
    f.mgr.pauseTask(t1->id());
    QTRY_COMPARE_WITH_TIMEOUT(t1->state(), Task::Paused, 5000);

    // Despite a free runner, t2 must NOT start: dispatch policy is
    // one-task-at-a-time per group. Spin the event loop briefly to give
    // a wrong implementation a chance to bind t2 to the idle runner;
    // assert it remained Queued.
    QTest::qWait(200);
    QCOMPARE(t2->state(), Task::Queued);

    f.mgr.resumeTask(t1->id());
    QVERIFY(f.waitForGroupRemoved(15000));

    QCOMPARE(f.lastStateOf(t1->id()), int(Task::Completed));
    QCOMPARE(f.lastStateOf(t2->id()), int(Task::Completed));
    QVERIFY(TestHelpers::filesEqual(f.path("a.bin"), f.path("a-out.bin")));
    QVERIFY(TestHelpers::filesEqual(f.path("b.bin"), f.path("b-out.bin")));
}

void TstTaskGroup::parallel_dispatch_across_groups() {
    TaskTestFixture f;                   // 2 workers
    TestHelpers::writeBytes(f.path("a.bin"), 4 * 1024 * 1024);
    TestHelpers::writeBytes(f.path("b.bin"), 4 * 1024 * 1024);

    auto* groupA = new TaskGroup(QStringLiteral("A"));
    auto* tA = addCopy(groupA, f.path("a.bin"), f.path("a-out.bin"));
    auto* groupB = new TaskGroup(QStringLiteral("B"));
    auto* tB = addCopy(groupB, f.path("b.bin"), f.path("b-out.bin"));

    f.mgr.enqueueGroup(groupA);
    f.mgr.enqueueGroup(groupB);

    // Pause both. Both reaching Paused proves both got dispatched —
    // i.e., different groups occupied separate workers concurrently.
    // (Polling for Running races on small files; pause is durable.)
    f.mgr.pauseTask(tA->id());
    f.mgr.pauseTask(tB->id());
    QTRY_VERIFY_WITH_TIMEOUT(
        tA->state() == Task::Paused && tB->state() == Task::Paused, 5000);

    f.mgr.resumeTask(tA->id());
    f.mgr.resumeTask(tB->id());
    QTRY_COMPARE_WITH_TIMEOUT(f.groupRemovedSpy.count(), 2, 15000);
    QCOMPARE(f.lastStateOf(tA->id()), int(Task::Completed));
    QCOMPARE(f.lastStateOf(tB->id()), int(Task::Completed));
}

void TstTaskGroup::path_touched_emits_for_completed_task() {
    TaskTestFixture f;
    QDir().mkpath(f.path("dest"));
    TestHelpers::writeBytes(f.path("src.bin"), 1024);

    auto* group = new TaskGroup(QStringLiteral("Touch"));
    addCopy(group, f.path("src.bin"), f.path("dest/dst.bin"));
    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupRemoved());

    // Manager fires pathTouched once per affectedDir on Completed (not
    // on Skipped). CopyFileTask::affectedDirs returns the dest's parent.
    bool sawDest = false;
    for (const QList<QVariant>& args : f.pathTouchedSpy) {
        if (QDir::cleanPath(args.value(0).toString())
                == QDir::cleanPath(f.path("dest"))) {
            sawDest = true;
            break;
        }
    }
    QVERIFY2(sawDest, "expected pathTouched to fire for the dest folder");
}

void TstTaskGroup::shutdown_is_idempotent() {
    // Explicit shutdown then dtor's shutdown — second call must be a
    // no-op. If it tried to quit/wait already-deleted threads we'd
    // crash here.
    TaskTestFixture f;
    f.mgr.shutdown();
    f.mgr.shutdown();   // no-op
    // Implicit second shutdown via TaskTestFixture dtor at scope end.
    QVERIFY(true);
}

void TstTaskGroup::shutdown_mid_run_returns_promptly() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 8 * 1024 * 1024);

    auto* group = new TaskGroup(QStringLiteral("Long"));
    auto* task = addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    f.mgr.enqueueGroup(group);

    // Pause to prove the runner is actually inside run() (not just
    // sitting in its event loop). Then shutdown must wake the pause CV
    // via stopAll, the task bails, run() returns, runner thread exits.
    f.mgr.pauseTask(task->id());
    QTRY_COMPARE_WITH_TIMEOUT(task->state(), Task::Paused, 5000);

    QElapsedTimer t;
    t.start();
    f.mgr.shutdown();
    const qint64 elapsed = t.elapsed();

    QVERIFY2(elapsed < 3000,
        qPrintable(QStringLiteral("shutdown took %1 ms — should resolve via "
            "stopAll waking the pause CV and the runner exiting").arg(elapsed)));
}

QTEST_GUILESS_MAIN(TstTaskGroup)
#include "tst_TaskGroup.moc"
