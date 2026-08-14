#ifndef TASKCONFLICTPROMPTER_H
#define TASKCONFLICTPROMPTER_H

#include <QList>
#include <QObject>
#include <QUuid>
#include <QVariantMap>

class QWidget;
class ThumbnailCache;

// GUI-thread controller that turns a task's conflict question into a blocking
// modal ConflictDialog. Replaces the old inline question strip in the task
// dock.
//
// Tasks in different groups run in parallel, so more than one conflict can be
// posted at once. This serialises them: questions queue FIFO and exactly one
// dialog is on screen at a time (each exec() runs a nested event loop, during
// which further questions simply append to the queue). A task that leaves the
// AwaitingAnswer state before its dialog is shown — e.g. the group was stopped
// — is dropped from the queue so no stale prompt appears.
class TaskConflictPrompter : public QObject {
    Q_OBJECT
public:
    // dialogParent is used to centre / own the modal dialog (typically the
    // MainWindow). thumbs is passed to each ConflictDialog for its side-by-side
    // image previews (may be null). Neither is owned.
    explicit TaskConflictPrompter(QWidget* dialogParent, ThumbnailCache* thumbs,
                                  QObject* parent = nullptr);

public slots:
    void onQuestionPosed(QUuid taskId, int kind, QVariantMap context);
    void onTaskStateChanged(QUuid taskId, int state);

signals:
    // Wired to TaskManager::provideAnswer.
    void answerProvided(QUuid taskId, int kind, int answer, bool applyToGroup);

private:
    void processQueue();

    struct Pending {
        QUuid taskId;
        int kind;
        QVariantMap context;
    };

    QWidget* _dialogParent;
    ThumbnailCache* _thumbs;
    QList<Pending> _queue;
    bool _busy = false;
};

#endif // TASKCONFLICTPROMPTER_H
