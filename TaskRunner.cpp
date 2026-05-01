#include "TaskRunner.h"

#include "Task.h"

TaskRunner::TaskRunner(QObject* parent) : QObject(parent) {}

void TaskRunner::runTask(Task* task) {
    if (task) {
        task->execute();
    }
    emit idle();
}
