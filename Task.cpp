#include "Task.h"

#include "TaskGroup.h"

Task::Task(TaskGroup* group, QObject* parent)
    : QObject(parent),
      _id(QUuid::createUuid()),
      _group(group),
      _completionEmitted(false) {
    _state.storeRelease(static_cast<int>(Queued));
    _stopRequested.storeRelease(0);
    _pauseRequested.storeRelease(0);
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
    setState(Running);
    emitProgress(0);

    run();

    // run() should have set _state to Completed/Failed/Aborted/Skipped
    // implicitly through helpers, OR we infer from the flags here.
    if (_completionEmitted) return;
    _completionEmitted = true;

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
