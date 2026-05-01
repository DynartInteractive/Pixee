#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
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

    // Submit a group. Manager takes ownership; the group will be deleted
    // when all its tasks reach a terminal state (or on shutdown).
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

    // Stops everything, drains workers, joins threads. Idempotent.
    void shutdown();

    // True iff at least one group is still in the queue (not all tasks have
    // reached a terminal state). Used by the UI to drive auto-show / auto-
    // hide of the tasks dock.
    bool hasGroups() const { return !_groups.isEmpty(); }

signals:
    void groupAdded(TaskGroup* group);
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
    void maybeRemoveGroup(TaskGroup* group);

    QList<RunnerSlot> _runners;
    QList<TaskGroup*> _groups;          // submission order
    bool _shutdown;
};

#endif // TASKMANAGER_H
