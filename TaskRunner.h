#ifndef TASKRUNNER_H
#define TASKRUNNER_H

#include <QObject>

class Task;

// Generic worker that executes a Task on its own thread. The TaskManager
// holds N runners; each lives on its own QThread and is invoked via a
// queued connection so the actual run() happens off the GUI thread.
class TaskRunner : public QObject
{
    Q_OBJECT
public:
    explicit TaskRunner(QObject* parent = nullptr);

public slots:
    // Called via QueuedConnection from the manager. Runs the task to
    // completion (which includes any pause / answer waits inside) and
    // emits idle() so the manager can dispatch the next task.
    void runTask(Task* task);

signals:
    // self pointer + the just-finished task (or nullptr on shutdown poke).
    // The manager uses sender() to identify the runner.
    void idle();
};

#endif // TASKRUNNER_H
