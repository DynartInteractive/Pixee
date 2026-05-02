#ifndef TASKDOCKWIDGET_H
#define TASKDOCKWIDGET_H

#include <QDockWidget>
#include <QHash>
#include <QUuid>

class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class TaskGroup;
class TaskGroupWidget;
class TaskManager;

// Right-side dock that hosts the live task list. Owns the per-group widgets
// and routes user clicks (pause/stop, etc.) back to the manager.
class TaskDockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit TaskDockWidget(TaskManager* manager, QWidget* parent = nullptr);

private slots:
    void onGroupAdded(TaskGroup* group);
    void onGroupRemoved(QUuid groupId);
    void onGroupFinished(QUuid groupId);
    void onTaskStateChanged(QUuid taskId, int state);
    void onTaskProgress(QUuid taskId, int pct);
    void onTaskQuestionPosed(QUuid taskId, int kind, QVariantMap ctx);

private:
    TaskGroupWidget* groupWidgetForTask(const QUuid& taskId) const;
    // Sync the "Clear all finished" enabled state to the manager's
    // current finished-group count. Cheap O(N) walk.
    void updateClearAllFinishedButton();

    TaskManager* _manager;
    QScrollArea* _scrollArea;
    QWidget* _container;
    QVBoxLayout* _containerLayout;
    QPushButton* _clearAllFinishedButton;

    QHash<QUuid, TaskGroupWidget*> _groupWidgets;  // groupId → widget
};

#endif // TASKDOCKWIDGET_H
