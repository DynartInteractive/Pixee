#ifndef TASKGROUP_H
#define TASKGROUP_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>

#include "Task.h"

// A group of related single-file tasks. Owns its tasks (via QObject parent)
// so deleting the group deletes the tasks. The group also holds the sticky
// answer map (e.g., "Skip All" applied across remaining tasks of the group)
// — answer reads happen on the worker thread, writes on the GUI thread, so
// the map is mutex-protected.
class TaskGroup : public QObject
{
    Q_OBJECT
public:
    enum State {
        Active,
        Paused,
        Stopped,
    };
    Q_ENUM(State)

    explicit TaskGroup(const QString& displayName, QObject* parent = nullptr);

    QUuid id() const { return _id; }
    QString displayName() const { return _displayName; }
    State state() const { return _state; }
    QVector<Task*> tasks() const { return _tasks; }

    // Manager calls this after constructing a task. Group takes parent
    // ownership.
    void addTask(Task* task);

    // Group-wide controls. Fan out to member tasks.
    void pauseAll();
    void resumeAll();
    void stopAll();

    // True iff every task is in a terminal state (Completed/Failed/Aborted/Skipped).
    bool allTerminal() const;

    // Sticky-answer accessors. Worker threads call stickyAnswer() while
    // running; GUI thread calls setStickyAnswer() when the user picks an
    // "All" variant.
    std::optional<Task::ConflictAnswer> stickyAnswer(int kind) const;
    void setStickyAnswer(int kind, Task::ConflictAnswer answer);

signals:
    void stateChanged(QUuid groupId, int state);

private:
    QUuid _id;
    QString _displayName;
    QVector<Task*> _tasks;
    State _state;

    mutable QMutex _stickyMutex;
    QHash<int, int> _stickyAnswers;  // value = ConflictAnswer cast to int
};

#endif // TASKGROUP_H
