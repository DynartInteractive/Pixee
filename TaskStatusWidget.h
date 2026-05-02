#ifndef TASKSTATUSWIDGET_H
#define TASKSTATUSWIDGET_H

#include <QTimer>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QProgressBar;
class TaskManager;

// Right-aligned status-bar permanent widget. Shows aggregate progress
// + an "X / Y" tasks-done label across every group the manager is
// tracking. Hidden when there's no active work; click toggles the
// task dock without touching the View → Tasks menu (the menu owns
// the persistent intent; this widget is the transient peek).
class TaskStatusWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TaskStatusWidget(TaskManager* manager, QWidget* parent = nullptr);

signals:
    // User clicked the widget. MainWindow handles the dock toggle —
    // this widget doesn't know about the dock, just about the manager.
    void toggleDockRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    // Recomputes counts + visibility from the manager's aggregate
    // accessors. Throttled via _refreshTimer so a tight chunked-IO
    // burst doesn't repaint per emit.
    void refresh();

private:
    TaskManager* _manager;
    QProgressBar* _bar;
    QLabel* _label;
    QTimer _refreshTimer;
};

#endif // TASKSTATUSWIDGET_H
