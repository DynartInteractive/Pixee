#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVariantMap>

class QThread;

#include "Task.h"

class TaskGroup;
class TaskRunner;

// Facade that owns a worker pool and a FIFO of submitted groups. Mirrors the
// shape of ThumbnailGenerator: one QThread per runner, queued dispatch,
// internal "request*" signals route work across threads. Lives on the GUI
// thread; the runners and their threads are children-by-pointer (manually
// quit/wait/delete in shutdown / dtor).
//
// Within a group, tasks run sequentially (one runner at a time per group).
// Across groups, runners parallelise up to the pool size. This keeps the
// conflict-question flow unambiguous: at most one task per group can be
// asking at any moment, so a "Skip All" answer always wins the next
// conflict in the same group.
class TaskManager : public QObject
{
    Q_OBJECT
public:
    explicit TaskManager(int workerCount, QObject* parent = nullptr);
    ~TaskManager() override;

    // Submit a group. Manager takes ownership; the group stays in the
    // tracked list once all its tasks reach a terminal state (signalled
    // via groupFinished) and is deleted only on clearGroup /
    // clearAllFinished / shutdown.
    void enqueueGroup(TaskGroup* group);

    // Group-level controls. Look up the group by id.
    void pauseGroup(const QUuid& groupId);
    void resumeGroup(const QUuid& groupId);
    void stopGroup(const QUuid& groupId);

    // Task-level controls. Look up the task by id.
    void pauseTask(const QUuid& taskId);
    void resumeTask(const QUuid& taskId);
    void stopTask(const QUuid& taskId);

    // Provide a conflict answer for a task that is awaiting one. If
    // applyToGroup is true, also store as the group's sticky answer for
    // this question kind.
    void provideAnswer(const QUuid& taskId, int kind, int answer, bool applyToGroup);

    // Explicit removal — moves the group out of _groups, emits
    // groupRemoved, and schedules deletion. Refuses (no-op) on a group
    // that still has non-terminal tasks; the UI's per-group Clear button
    // is only shown for terminal groups, but the guard keeps the API
    // safe to call regardless.
    void clearGroup(const QUuid& groupId);

    // Bulk clear: drops every group whose tasks are all terminal.
    // Running / paused / queued groups are untouched.
    void clearAllFinished();

    // Stops everything, drains workers, joins threads. Idempotent.
    void shutdown();

    // True iff at least one group is still tracked. After Phase 1 of the
    // dock-rework, finished groups stay in _groups until explicitly
    // cleared, so this no longer means "work is in progress" — see
    // hasActiveTasks for that.
    bool hasGroups() const { return !_groups.isEmpty(); }

    // True iff any task in any group is in a non-terminal state. Drives
    // the status-bar progress widget's visibility and the auto-hide-on-
    // empty behaviour for status-bar-opened docks.
    bool hasActiveTasks() const;

    // True iff at least one tracked group has reached all-terminal and
    // hasn't been cleared yet. Drives the dock's "Clear all finished"
    // button enabled state.
    bool hasFinishedGroups() const;

    // Aggregate counters for the status-bar widget. terminalTaskCount
    // includes Completed + Failed + Aborted + Skipped. aggregateProgress
    // sums per-task progress (terminal = 100, others = last reported)
    // divided by total task count. Returns 0 when there are no tasks at
    // all (used as the "hide me" sentinel by the widget).
    int totalTaskCount() const;
    int terminalTaskCount() const;
    int aggregateProgressPercent() const;

signals:
    void groupAdded(TaskGroup* group);
    // Emitted once when every task in a group reaches a terminal state.
    // The group itself stays in the manager's list until clearGroup or
    // clearAllFinished removes it. Use this signal (not groupRemoved)
    // to know "this batch is done".
    void groupFinished(QUuid groupId);
    // Emitted only when a group is explicitly removed (clearGroup /
    // clearAllFinished / shutdown). Use to take down the per-group UI.
    void groupRemoved(QUuid groupId);
    void taskStateChanged(QUuid taskId, int state);
    void taskProgress(QUuid taskId, int pct);
    void taskQuestionPosed(QUuid taskId, int kind, QVariantMap context);
    // Emitted once per directory the just-completed task says was affected.
    // The UI debounces these and refreshes the corresponding folder.
    void pathTouched(QString dir);

private slots:
    void onRunnerIdle();
    void onTaskFinished(QUuid taskId);
    void onTaskFailed(QUuid taskId, QString message);
    void onTaskAborted(QUuid taskId);

private:
    struct RunnerSlot {
        QThread* thread;
        TaskRunner* runner;
        Task* current;     // the task currently bound to this runner (nullptr if idle)
        QUuid groupId;     // group owning current; QUuid() if idle
    };

    void wireTask(Task* task);
    void dispatch();
    Task* nextRunnableTaskFor(const QUuid& busyGroupId,
                              const QSet<QUuid>& groupsCurrentlyRunning);
    Task* findTask(const QUuid& taskId) const;
    TaskGroup* findGroup(const QUuid& groupId) const;
    void onTaskTerminal(const QUuid& taskId);
    void freeRunnerForTask(const QUuid& taskId);
    // Renamed from maybeRemoveGroup — fires groupFinished but leaves the
    // group in _groups for explicit clear. Tracks groups already finished
    // so we don't re-fire on subsequent terminal transitions in the same
    // group (e.g. an already-finished group whose state we re-check).
    void maybeFinishGroup(TaskGroup* group);

    QList<RunnerSlot> _runners;
    QList<TaskGroup*> _groups;          // submission order
    QSet<QUuid> _finishedGroups;        // groups that already emitted groupFinished
    QHash<QUuid, int> _taskProgress;    // taskId → last reported pct, for aggregate
    bool _shutdown;
};

#endif // TASKMANAGER_H
