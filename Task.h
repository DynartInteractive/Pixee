#ifndef TASK_H
#define TASK_H

#include <QAtomicInt>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <QWaitCondition>

#include <optional>

class TaskGroup;

// Abstract single-file operation. Subclasses override run() to perform the
// actual work; run() must call checkPauseStop() at chunk boundaries so the
// task is responsive to pause / stop, and emit progress(0..100) as it goes.
//
// Threading model: the task is created on the GUI thread (where the manager
// lives), then the worker thread invokes execute() — which calls run() and
// emits the appropriate terminal signal. The task itself never moves
// threads; cross-thread coordination uses QAtomicInt flags + per-task
// QMutex/QWaitCondition pairs.
class Task : public QObject
{
    Q_OBJECT
public:
    enum State {
        Queued,
        Running,
        Paused,
        AwaitingAnswer,
        Completed,
        Failed,
        Aborted,
        Skipped,
    };
    Q_ENUM(State)

    enum QuestionKind {
        DestinationExists = 0,
    };
    Q_ENUM(QuestionKind)

    enum ConflictAnswer {
        Skip = 0,
        Overwrite,
        Rename,
    };
    Q_ENUM(ConflictAnswer)

    explicit Task(TaskGroup* group, QObject* parent = nullptr);
    ~Task() override;

    QUuid id() const { return _id; }
    QUuid groupId() const;
    TaskGroup* group() const { return _group; }
    State state() const { return static_cast<State>(_state.loadAcquire()); }
    virtual QString displayName() const = 0;

    // Directories whose contents may have changed because this task ran.
    // Returned to the manager on successful completion so the UI can know
    // to refresh the affected folder. Default returns nothing so unknown
    // subclasses don't trigger spurious refreshes.
    virtual QStringList affectedDirs() const { return {}; }

    // Called from the GUI thread. All thread-safe.
    void requestPause();
    void requestResume();
    void requestStop();
    void provideAnswer(ConflictAnswer answer);

    // Called from the worker thread that owns the task for the duration of
    // the run. Drives run(), then emits exactly one terminal signal.
    void execute();

signals:
    // All carry the task's id so the UI can route updates to the right row
    // without holding a Task* (which lives on the GUI thread anyway).
    void stateChanged(QUuid id, int state);
    void progress(QUuid id, int pct);
    void needsAnswer(QUuid id, int kind, QVariantMap context);
    void finished(QUuid id);
    void failed(QUuid id, QString message);
    void aborted(QUuid id);

protected:
    // Subclasses do their real work here. Must call checkPauseStop()
    // periodically — at every chunk boundary at minimum — and may call
    // emitProgress() / setFailed() / setSkipped() / resolveOrAsk().
    virtual void run() = 0;

    // Returns false if the run should bail out (stop requested). Blocks
    // while pause is requested. Safe to call from inside the worker.
    bool checkPauseStop();

    void emitProgress(int pct);
    void setFailed(const QString& message);
    void setSkipped();

    // Consults the group's sticky answer for this kind first; if absent,
    // emits needsAnswer and blocks the worker until provideAnswer() is
    // called from the GUI thread (or stop is requested, in which case
    // returns Skip as a safe default — caller should also check stop).
    ConflictAnswer resolveOrAsk(QuestionKind kind, const QVariantMap& context);

    bool isStopRequested() const { return _stopRequested.loadAcquire() != 0; }

private:
    void setState(State s);

    QUuid _id;
    TaskGroup* _group;
    // _state is written by the worker thread (via setState in run/execute)
    // and read by the GUI thread (via state() during manager dispatch), so
    // it's atomic. Stored as int because QAtomicInt doesn't take enums.
    QAtomicInt _state;
    QString _failureMessage;
    bool _completionEmitted;

    QAtomicInt _stopRequested;
    QAtomicInt _pauseRequested;

    QMutex _pauseMutex;
    QWaitCondition _pauseCv;

    QMutex _answerMutex;
    QWaitCondition _answerCv;
    std::optional<ConflictAnswer> _pendingAnswer;
};

#endif // TASK_H
