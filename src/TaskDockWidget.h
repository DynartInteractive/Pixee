#ifndef TASKDOCKWIDGET_H
#define TASKDOCKWIDGET_H

#include <QDockWidget>
#include <QHash>
#include <QUuid>

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
    void onTaskStateChanged(QUuid taskId, int state);
    void onTaskProgress(QUuid taskId, int pct);

private:
    TaskGroupWidget* groupWidgetForTask(const QUuid& taskId) const;

    TaskManager* _manager;
    QScrollArea* _scrollArea;
    QWidget* _container;
    QVBoxLayout* _containerLayout;

    QHash<QUuid, TaskGroupWidget*> _groupWidgets;  // groupId → widget
};

#endif // TASKDOCKWIDGET_H
