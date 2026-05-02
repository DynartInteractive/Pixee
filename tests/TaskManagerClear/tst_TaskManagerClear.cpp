// Locks in the Phase 1 dock-rework behaviour: groupFinished fires when
// the last task terminates, but the group stays in the manager until
// the user explicitly clears it (per-group or via clearAllFinished).

#include <QFile>
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

class TstTaskManagerClear : public QObject {
    Q_OBJECT

private slots:
    void finished_group_stays_in_manager();
    void clear_group_removes_terminal_group_and_emits_groupRemoved();
    void clear_group_refuses_a_running_group();
    void clear_all_finished_removes_terminal_groups_only();
    void has_active_tasks_flips_at_terminal();
    void aggregate_counters_reflect_lifecycle();
    void aggregate_counters_exclude_finished_groups();
    void shutdown_drops_finished_groups_without_groupRemoved();
};

namespace {

CopyFileTask* addCopy(TaskGroup* group, const QString& src, const QString& dst) {
    auto* t = new CopyFileTask(src, dst, group);
    group->addTask(t);
    return t;
}

}

// ---------------------------------------------------------------------------

void TstTaskManagerClear::finished_group_stays_in_manager() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 1024);

    auto* group = new TaskGroup(QStringLiteral("One"));
    addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    f.mgr.enqueueGroup(group);

    QVERIFY(f.waitForGroupFinished());
    QCOMPARE(f.groupFinishedSpy.count(), 1);

    // Group is still tracked; groupRemoved did NOT fire.
    QVERIFY(f.mgr.hasGroups());
    QCOMPARE(f.groupRemovedSpy.count(), 0);
    QVERIFY(!f.mgr.hasActiveTasks());
}

void TstTaskManagerClear::clear_group_removes_terminal_group_and_emits_groupRemoved() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 1024);

    auto* group = new TaskGroup(QStringLiteral("One"));
    addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    const QUuid groupId = group->id();
    f.mgr.enqueueGroup(group);

    QVERIFY(f.waitForGroupFinished());

    f.mgr.clearGroup(groupId);
    QVERIFY(f.waitForGroupRemoved());
    QCOMPARE(f.groupRemovedSpy.first().value(0).toUuid(), groupId);
    QVERIFY(!f.mgr.hasGroups());
}

void TstTaskManagerClear::clear_group_refuses_a_running_group() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 4 * 1024 * 1024);

    auto* group = new TaskGroup(QStringLiteral("Slow"));
    auto* task = addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    const QUuid groupId = group->id();
    f.mgr.enqueueGroup(group);

    // Pause the task so the group is in a stable non-terminal state.
    f.mgr.pauseTask(task->id());
    QTRY_COMPARE_WITH_TIMEOUT(task->state(), Task::Paused, 5000);

    // clearGroup must refuse silently; the group stays.
    f.mgr.clearGroup(groupId);
    QVERIFY(f.mgr.hasGroups());
    QCOMPARE(f.groupRemovedSpy.count(), 0);

    // Resume + finish + clean clear so the test exits tidily.
    f.mgr.resumeTask(task->id());
    QVERIFY(f.waitForGroupFinished(15000));
    f.mgr.clearGroup(groupId);
}

void TstTaskManagerClear::clear_all_finished_removes_terminal_groups_only() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("a.bin"), 1024);
    TestHelpers::writeBytes(f.path("b.bin"), 4 * 1024 * 1024);

    // Group A — short, will finish immediately.
    auto* groupA = new TaskGroup(QStringLiteral("A"));
    addCopy(groupA, f.path("a.bin"), f.path("a-out.bin"));
    f.mgr.enqueueGroup(groupA);
    QVERIFY(f.waitForGroupFinished());

    // Group B — paused mid-flight, stays running.
    auto* groupB = new TaskGroup(QStringLiteral("B"));
    auto* taskB = addCopy(groupB, f.path("b.bin"), f.path("b-out.bin"));
    const QUuid groupBId = groupB->id();
    f.mgr.enqueueGroup(groupB);
    f.mgr.pauseTask(taskB->id());
    QTRY_COMPARE_WITH_TIMEOUT(taskB->state(), Task::Paused, 5000);

    f.mgr.clearAllFinished();

    // A removed; B kept.
    QCOMPARE(f.groupRemovedSpy.count(), 1);
    QCOMPARE(f.groupRemovedSpy.first().value(0).toUuid(), groupA->id());
    QCOMPARE(f.mgr.totalTaskCount(), 1);          // only B's task

    // Resume + clear B.
    f.mgr.resumeTask(taskB->id());
    QTRY_COMPARE_WITH_TIMEOUT(f.groupFinishedSpy.count(), 2, 15000);
    f.mgr.clearGroup(groupBId);
    QVERIFY(!f.mgr.hasGroups());
}

void TstTaskManagerClear::has_active_tasks_flips_at_terminal() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 1024);

    QVERIFY(!f.mgr.hasActiveTasks());

    auto* group = new TaskGroup(QStringLiteral("One"));
    addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    f.mgr.enqueueGroup(group);

    // We can't reliably observe Active during the run (1 KB completes
    // in microseconds), so rely on the post-finish state:
    QVERIFY(f.waitForGroupFinished());
    QVERIFY(!f.mgr.hasActiveTasks());

    // Even though the group is still tracked.
    QVERIFY(f.mgr.hasGroups());
}

void TstTaskManagerClear::aggregate_counters_reflect_lifecycle() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("a.bin"), 1024);
    TestHelpers::writeBytes(f.path("b.bin"), 1024);

    QCOMPARE(f.mgr.totalTaskCount(), 0);
    QCOMPARE(f.mgr.terminalTaskCount(), 0);
    QCOMPARE(f.mgr.aggregateProgressPercent(), 0);

    auto* group = new TaskGroup(QStringLiteral("Two"));
    addCopy(group, f.path("a.bin"), f.path("a-out.bin"));
    addCopy(group, f.path("b.bin"), f.path("b-out.bin"));
    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    // Finished groups are excluded from the aggregate counters — the
    // status-bar widget should hide once everything's done, not show
    // 2 / 2 forever until cleared.
    QCOMPARE(f.mgr.totalTaskCount(), 0);
    QCOMPARE(f.mgr.terminalTaskCount(), 0);
    QCOMPARE(f.mgr.aggregateProgressPercent(), 0);

    f.mgr.clearGroup(group->id());
    QCOMPARE(f.mgr.totalTaskCount(), 0);
    QCOMPARE(f.mgr.aggregateProgressPercent(), 0);
}

void TstTaskManagerClear::aggregate_counters_exclude_finished_groups() {
    // Finished group A still in the dock; running group B with the
    // first task paused. Aggregate should reflect ONLY B's tasks so
    // a stale A doesn't dilute the X / Y label.
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("a.bin"), 1024);
    TestHelpers::writeBytes(f.path("b1.bin"), 4 * 1024 * 1024);
    TestHelpers::writeBytes(f.path("b2.bin"), 1024);

    auto* groupA = new TaskGroup(QStringLiteral("A"));
    addCopy(groupA, f.path("a.bin"), f.path("a-out.bin"));
    f.mgr.enqueueGroup(groupA);
    QVERIFY(f.waitForGroupFinished());
    QVERIFY(f.mgr.hasGroups());                   // A still tracked

    auto* groupB = new TaskGroup(QStringLiteral("B"));
    auto* tB1 = addCopy(groupB, f.path("b1.bin"), f.path("b1-out.bin"));
    addCopy(groupB, f.path("b2.bin"), f.path("b2-out.bin"));
    f.mgr.enqueueGroup(groupB);

    f.mgr.pauseTask(tB1->id());
    QTRY_COMPARE_WITH_TIMEOUT(tB1->state(), Task::Paused, 5000);

    QCOMPARE(f.mgr.totalTaskCount(), 2);          // only B's two tasks
    QCOMPARE(f.mgr.terminalTaskCount(), 0);       // neither finished

    f.mgr.resumeTask(tB1->id());
    QTRY_COMPARE_WITH_TIMEOUT(f.groupFinishedSpy.count(), 2, 15000);

    // Both groups finished now — aggregate is empty again.
    QCOMPARE(f.mgr.totalTaskCount(), 0);
    QCOMPARE(f.mgr.aggregateProgressPercent(), 0);
}

void TstTaskManagerClear::shutdown_drops_finished_groups_without_groupRemoved() {
    TaskTestFixture f;
    TestHelpers::writeBytes(f.path("src.bin"), 1024);

    auto* group = new TaskGroup(QStringLiteral("One"));
    addCopy(group, f.path("src.bin"), f.path("dst.bin"));
    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    // Shutdown drops the group but does NOT emit groupRemoved per the
    // current contract — listeners are likely going down anyway and the
    // explicit-clear pathway is the only one that fires the signal.
    const int beforeRemoveCount = f.groupRemovedSpy.count();
    f.mgr.shutdown();
    QCOMPARE(f.groupRemovedSpy.count(), beforeRemoveCount);
    QVERIFY(!f.mgr.hasGroups());
}

QTEST_GUILESS_MAIN(TstTaskManagerClear)
#include "tst_TaskManagerClear.moc"
