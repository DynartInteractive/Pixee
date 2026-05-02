#include "Task.h"

#include "TaskGroup.h"

Task::Task(TaskGroup* group, QObject* parent)
    : QObject(parent),
      _id(QUuid::createUuid()),
      _group(group) {
    _state.storeRelease(static_cast<int>(Queued));
    _stopRequested.storeRelease(0);
    _pauseRequested.storeRelease(0);
    _completionEmitted.storeRelease(0);
}

Task::~Task() = default;

QUuid Task::groupId() const {
    return _group ? _group->id() : QUuid();
}

void Task::setState(State s) {
    const int old = _state.fetchAndStoreRelease(static_cast<int>(s));
    if (old != static_cast<int>(s)) {
        emit stateChanged(_id, static_cast<int>(s));
    }
}

void Task::emitProgress(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    emit progress(_id, pct);
}

void Task::setFailed(const QString& message) {
    _failureMessage = message;
}

void Task::setSkipped() {
    setState(Skipped);
}

void Task::requestPause() {
    _pauseRequested.storeRelease(1);
    // No wake needed — the worker enters the wait at its next checkPauseStop.
}

void Task::requestResume() {
    QMutexLocker lock(&_pauseMutex);
    _pauseRequested.storeRelease(0);
    _pauseCv.wakeAll();
}

void Task::requestStop() {
    _stopRequested.storeRelease(1);
    {
        QMutexLocker lock(&_pauseMutex);
        _pauseCv.wakeAll();
    }
    {
        QMutexLocker lock(&_answerMutex);
        _answerCv.wakeAll();
    }
    // Queued tasks haven't been picked up by a runner yet — and never
    // will be, since dispatch skips Stopped groups. Without this they
    // stay in Queued forever, blocking allTerminal() from ever flipping
    // and the group from ever being removed from the dock. CAS so we
    // don't double-emit if a runner happens to be racing us.
    if (state() == Queued
            && _completionEmitted.testAndSetAcquire(0, 1)) {
        setState(Aborted);
        emit aborted(_id);
    }
}

void Task::provideAnswer(ConflictAnswer answer) {
    QMutexLocker lock(&_answerMutex);
    _pendingAnswer = answer;
    _answerCv.wakeAll();
}

bool Task::checkPauseStop() {
    if (_stopRequested.loadAcquire()) return false;
    if (_pauseRequested.loadAcquire()) {
        QMutexLocker lock(&_pauseMutex);
        if (_pauseRequested.loadAcquire() && !_stopRequested.loadAcquire()) {
            // setState emits, but emit-from-worker-thread is fine; receivers
            // get queued delivery on the GUI thread.
            setState(Paused);
            while (_pauseRequested.loadAcquire() && !_stopRequested.loadAcquire()) {
                _pauseCv.wait(&_pauseMutex);
            }
            // Restore the running state after resume — unless we were woken
            // by stop, in which case the caller will see false below.
            if (!_stopRequested.loadAcquire()) {
                setState(Running);
            }
        }
    }
    return !_stopRequested.loadAcquire();
}

Task::ConflictAnswer Task::resolveOrAsk(QuestionKind kind, const QVariantMap& context) {
    if (_group) {
        const auto sticky = _group->stickyAnswer(static_cast<int>(kind));
        if (sticky.has_value()) {
            return sticky.value();
        }
    }

    setState(AwaitingAnswer);
    emit needsAnswer(_id, static_cast<int>(kind), context);

    QMutexLocker lock(&_answerMutex);
    _pendingAnswer.reset();
    while (!_pendingAnswer.has_value() && !_stopRequested.loadAcquire()) {
        _answerCv.wait(&_answerMutex);
    }

    setState(Running);
    if (_stopRequested.loadAcquire()) {
        return Skip;  // caller checks isStopRequested() and bails out
    }
    return _pendingAnswer.value();
}

void Task::execute() {
    // Claim the run atomically. CAS 0→2 fails if requestStop already
    // force-aborted us (sentinel == 1) — bail without setState(Running)
    // (which would overwrite the Aborted state) and without re-emitting.
    if (!_completionEmitted.testAndSetAcquire(0, 2)) return;

    setState(Running);
    emitProgress(0);

    run();

    // We own the terminal emit (CAS at top set sentinel to 2).
    if (_stopRequested.loadAcquire()) {
        setState(Aborted);
        emit aborted(_id);
        return;
    }
    if (!_failureMessage.isEmpty()) {
        setState(Failed);
        emit failed(_id, _failureMessage);
        return;
    }
    if (state() == Skipped) {
        // Treat skipped as a successful terminal — emit finished so the
        // manager unwires the runner.
        emit finished(_id);
        return;
    }
    setState(Completed);
    emitProgress(100);
    emit finished(_id);
}
