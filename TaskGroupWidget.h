#ifndef TASKGROUPWIDGET_H
#define TASKGROUPWIDGET_H

#include <QFrame>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QUuid>

class QLabel;
class QProgressBar;
class QToolButton;
class QVBoxLayout;
class TaskGroup;
class TaskItemWidget;

// Header (collapse arrow + name + aggregate progress + pause/stop) on top
// of a body containing one TaskItemWidget per task in the group.
class TaskGroupWidget : public QFrame
{
    Q_OBJECT
public:
    explicit TaskGroupWidget(TaskGroup* group, QWidget* parent = nullptr);

    QUuid groupId() const { return _groupId; }
    TaskItemWidget* itemFor(const QUuid& taskId) const { return _items.value(taskId, nullptr); }

    // Manager forwards updates here for member tasks.
    void onTaskProgress(const QUuid& taskId, int pct);
    void onTaskStateChanged(const QUuid& taskId, int state);
    void onTaskQuestionPosed(const QUuid& taskId, int kind, const QVariantMap& ctx);

signals:
    void pauseGroupRequested(QUuid groupId);
    void resumeGroupRequested(QUuid groupId);
    void stopGroupRequested(QUuid groupId);
    void pauseTaskRequested(QUuid taskId);
    void resumeTaskRequested(QUuid taskId);
    void stopTaskRequested(QUuid taskId);
    void answerProvided(QUuid taskId, int kind, int answer, bool applyToGroup);

private:
    void rebuildAggregateProgress();
    void updateGroupPauseButton();

    QUuid _groupId;
    QToolButton* _toggleButton;
    QLabel* _nameLabel;
    QProgressBar* _aggregate;
    QToolButton* _pauseGroupButton;
    QToolButton* _stopGroupButton;
    QFrame* _body;
    QVBoxLayout* _bodyLayout;

    QHash<QUuid, TaskItemWidget*> _items;
    QHash<QUuid, int> _progress;     // for aggregate
    QHash<QUuid, int> _states;       // last-seen state per task
    bool _expanded;
    bool _groupPaused;
    // Throttle aggregate progress updates so a tight chunked-IO loop
    // (one progress emit per 64 KB on a fast disk = thousands per second)
    // doesn't repaint the bar at every emit.
    QTimer _aggregateTimer;
};

#endif // TASKGROUPWIDGET_H
