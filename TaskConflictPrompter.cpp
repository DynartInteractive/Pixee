#include "TaskConflictPrompter.h"

#include "ConflictDialog.h"
#include "Task.h"

TaskConflictPrompter::TaskConflictPrompter(QWidget* dialogParent, QObject* parent)
    : QObject(parent), _dialogParent(dialogParent) {}

void TaskConflictPrompter::onQuestionPosed(QUuid taskId, int kind, QVariantMap context) {
    _queue.append({ taskId, kind, context });
    processQueue();
}

void TaskConflictPrompter::onTaskStateChanged(QUuid taskId, int state) {
    // A queued question whose task is no longer waiting (stopped / aborted /
    // otherwise resolved) must not surface a stale dialog. The currently
    // shown dialog isn't in the queue, so this only touches not-yet-shown
    // entries — which is all we can safely retract.
    if (state == Task::AwaitingAnswer) return;
    for (int i = _queue.size() - 1; i >= 0; --i) {
        if (_queue.at(i).taskId == taskId) _queue.removeAt(i);
    }
}

void TaskConflictPrompter::processQueue() {
    // Guard against re-entry: each exec() below spins a nested event loop, so
    // onQuestionPosed can fire again and append while we're mid-dialog. The
    // while-loop picks those up once the current dialog closes.
    if (_busy) return;
    _busy = true;

    while (!_queue.isEmpty()) {
        const Pending p = _queue.takeFirst();

        ConflictDialog dialog(p.kind, p.context, _dialogParent);
        dialog.exec();

        emit answerProvided(p.taskId, p.kind,
                            static_cast<int>(dialog.answer()), dialog.applyToAll());
    }

    _busy = false;
}
