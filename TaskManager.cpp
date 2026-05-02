#include "TaskManager.h"

#include <QMetaObject>
#include <QSet>
#include <QThread>

#include "TaskGroup.h"
#include "TaskRunner.h"

TaskManager::TaskManager(int workerCount, QObject* parent)
    : QObject(parent), _shutdown(false) {
    // Required for QMetaObject::invokeMethod with Q_ARG(Task*, ...) across
    // threads — Qt has to know how to box the pointer for the queued call.
    qRegisterMetaType<Task*>("Task*");

    if (workerCount < 1) workerCount = 1;
    _runners.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        QThread* t = new QThread();
        TaskRunner* r = new TaskRunner();
        r->moveToThread(t);
        connect(t, &QThread::finished, r, &QObject::deleteLater);
        connect(r, &TaskRunner::idle, this, &TaskManager::onRunnerIdle);
        t->start();
        _runners.append({ t, r, nullptr, QUuid() });
    }
}

TaskManager::~TaskManager() {
    shutdown();
}

void TaskManager::shutdown() {
    if (_shutdown) return;
    _shutdown = true;

    // Tell every group / task to stop. Wakes any pause / answer waits.
    for (TaskGroup* g : _groups) g->stopAll();

    // Quit + wait each runner thread.
    for (auto& slot : _runners) {
        if (slot.thread) slot.thread->quit();
    }
    for (auto& slot : _runners) {
        if (slot.thread) {
            slot.thread->wait();
            delete slot.thread;
            slot.thread = nullptr;
            slot.runner = nullptr;
        }
    }

    // Delete remaining groups (groups own their tasks via QObject parent).
    qDeleteAll(_groups);
    _groups.clear();
    _taskProgress.clear();
}

void TaskManager::wireTask(Task* task) {
    connect(task, &Task::stateChanged, this,
            [this](QUuid id, int state) { emit taskStateChanged(id, state); });
    connect(task, &Task::progress, this,
            [this](QUuid id, int pct) {
                _taskProgress.insert(id, pct);
                emit taskProgress(id, pct);
            });
    connect(task, &Task::needsAnswer, this,
            [this](QUuid id, int kind, QVariantMap ctx) { emit taskQuestionPosed(id, kind, ctx); });
    connect(task, &Task::finished, this, &TaskManager::onTaskFinished);
    connect(task, &Task::failed, this, &TaskManager::onTaskFailed);
    connect(task, &Task::aborted, this, &TaskManager::onTaskAborted);
}

void TaskManager::enqueueGroup(TaskGroup* group) {
    if (!group || _shutdown) {
        if (group) group->deleteLater();
        return;
    }
    group->setParent(this);
    for (Task* t : group->tasks()) wireTask(t);
    _groups.append(group);
    emit groupAdded(group);
    dispatch();
}

Task* TaskManager::findTask(const QUuid& taskId) const {
    for (TaskGroup* g : _groups) {
        for (Task* t : g->tasks()) {
            if (t->id() == taskId) return t;
        }
    }
    return nullptr;
}

TaskGroup* TaskManager::findGroup(const QUuid& groupId) const {
    for (TaskGroup* g : _groups) {
        if (g->id() == groupId) return g;
    }
    return nullptr;
}

void TaskManager::pauseGroup(const QUuid& groupId) {
    if (TaskGroup* g = findGroup(groupId)) g->pauseAll();
}

void TaskManager::resumeGroup(const QUuid& groupId) {
    if (TaskGroup* g = findGroup(groupId)) {
        g->resumeAll();
        dispatch();
    }
}

void TaskManager::stopGroup(const QUuid& groupId) {
    if (TaskGroup* g = findGroup(groupId)) {
        g->stopAll();
        // Tasks that hadn't started yet will be drained from the queue at
        // the next dispatch since their group is in Stopped state. Running
        // tasks bail at their next checkPauseStop.
    }
}

void TaskManager::pauseTask(const QUuid& taskId) {
    if (Task* t = findTask(taskId)) t->requestPause();
}

void TaskManager::resumeTask(const QUuid& taskId) {
    if (Task* t = findTask(taskId)) {
        t->requestResume();
        dispatch();
    }
}

void TaskManager::stopTask(const QUuid& taskId) {
    if (Task* t = findTask(taskId)) t->requestStop();
}

void TaskManager::provideAnswer(const QUuid& taskId, int kind, int answer, bool applyToGroup) {
    Task* t = findTask(taskId);
    if (!t) return;
    if (applyToGroup && t->group()) {
        t->group()->setStickyAnswer(kind, static_cast<Task::ConflictAnswer>(answer));
    }
    t->provideAnswer(static_cast<Task::ConflictAnswer>(answer));
}

void TaskManager::onRunnerIdle() {
    TaskRunner* r = qobject_cast<TaskRunner*>(sender());
    if (!r) return;
    for (auto& slot : _runners) {
        if (slot.runner == r) {
            slot.current = nullptr;
            slot.groupId = QUuid();
            break;
        }
    }
    dispatch();
}

void TaskManager::onTaskFinished(QUuid taskId) {
    Task* t = findTask(taskId);
    // Only emit pathTouched on real completion, not on Skipped — a skipped
    // task by definition didn't change anything on disk. finished() also
    // fires for Skipped, so we have to filter here.
    if (t && t->state() == Task::Completed) {
        for (const QString& dir : t->affectedDirs()) emit pathTouched(dir);
    }
    onTaskTerminal(taskId);
}

void TaskManager::onTaskFailed(QUuid taskId, QString /*message*/) {
    onTaskTerminal(taskId);
}

void TaskManager::onTaskAborted(QUuid taskId) {
    onTaskTerminal(taskId);
}

void TaskManager::onTaskTerminal(const QUuid& taskId) {
    // The task itself signaled its terminal state. The runner-idle slot
    // independently clears the runner slot. Here we only check if the
    // owning group is fully done so we can remove it.
    Task* t = findTask(taskId);
    if (!t) return;
    if (TaskGroup* g = t->group()) maybeRemoveGroup(g);
}

void TaskManager::maybeRemoveGroup(TaskGroup* group) {
    if (!group) return;
    if (!group->allTerminal()) return;
    const QUuid id = group->id();
    _groups.removeAll(group);
    for (Task* t : group->tasks()) _taskProgress.remove(t->id());
    emit groupRemoved(id);
    group->deleteLater();
}

namespace {
bool isTerminalState(int state) {
    return state == Task::Completed || state == Task::Failed
        || state == Task::Aborted   || state == Task::Skipped;
}
}

int TaskManager::totalTaskCount() const {
    int n = 0;
    for (TaskGroup* g : _groups) n += g->tasks().size();
    return n;
}

int TaskManager::terminalTaskCount() const {
    int n = 0;
    for (TaskGroup* g : _groups) {
        for (Task* t : g->tasks()) {
            if (isTerminalState(static_cast<int>(t->state()))) ++n;
        }
    }
    return n;
}

int TaskManager::aggregateProgressPercent() const {
    int total = 0;
    int n = 0;
    for (TaskGroup* g : _groups) {
        for (Task* t : g->tasks()) {
            const int s = static_cast<int>(t->state());
            const int pct = isTerminalState(s) ? 100
                          : _taskProgress.value(t->id(), 0);
            total += pct;
            ++n;
        }
    }
    if (n == 0) return 0;
    return total / n;
}

Task* TaskManager::nextRunnableTaskFor(const QUuid& /*busyGroupId*/,
                                       const QSet<QUuid>& groupsCurrentlyRunning) {
    // Walk groups in submission order, pick the first task in the first
    // group that (a) has no task currently running on any other runner,
    // and (b) is in Active state. Tasks within a group run sequentially.
    for (TaskGroup* g : _groups) {
        if (g->state() != TaskGroup::Active) continue;
        if (groupsCurrentlyRunning.contains(g->id())) continue;
        for (Task* t : g->tasks()) {
            if (t->state() == Task::Queued) return t;
        }
    }
    return nullptr;
}

void TaskManager::dispatch() {
    if (_shutdown) return;

    QSet<QUuid> running;
    for (const auto& slot : _runners) {
        if (slot.current) running.insert(slot.groupId);
    }

    for (auto& slot : _runners) {
        if (slot.current) continue;
        Task* t = nextRunnableTaskFor(QUuid(), running);
        if (!t) break;
        slot.current = t;
        slot.groupId = t->groupId();
        running.insert(slot.groupId);
        QMetaObject::invokeMethod(slot.runner, "runTask", Qt::QueuedConnection,
            Q_ARG(Task*, t));
    }
}

