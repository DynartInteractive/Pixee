#include "TaskGroup.h"

TaskGroup::TaskGroup(const QString& displayName, QObject* parent)
    : QObject(parent),
      _id(QUuid::createUuid()),
      _displayName(displayName),
      _state(Active) {}

void TaskGroup::addTask(Task* task) {
    if (!task) return;
    task->setParent(this);
    _tasks.append(task);
}

void TaskGroup::pauseAll() {
    if (_state == Stopped) return;
    _state = Paused;
    emit stateChanged(_id, static_cast<int>(_state));
    for (Task* t : _tasks) t->requestPause();
}

void TaskGroup::resumeAll() {
    if (_state == Stopped) return;
    _state = Active;
    emit stateChanged(_id, static_cast<int>(_state));
    for (Task* t : _tasks) t->requestResume();
}

void TaskGroup::stopAll() {
    _state = Stopped;
    emit stateChanged(_id, static_cast<int>(_state));
    for (Task* t : _tasks) t->requestStop();
}

bool TaskGroup::allTerminal() const {
    for (Task* t : _tasks) {
        const Task::State s = t->state();
        if (s != Task::Completed && s != Task::Failed
                && s != Task::Aborted && s != Task::Skipped) {
            return false;
        }
    }
    return true;
}

std::optional<Task::ConflictAnswer> TaskGroup::stickyAnswer(int kind) const {
    QMutexLocker lock(&_stickyMutex);
    const auto it = _stickyAnswers.constFind(kind);
    if (it == _stickyAnswers.constEnd()) return std::nullopt;
    return static_cast<Task::ConflictAnswer>(it.value());
}

void TaskGroup::setStickyAnswer(int kind, Task::ConflictAnswer answer) {
    QMutexLocker lock(&_stickyMutex);
    _stickyAnswers.insert(kind, static_cast<int>(answer));
}
