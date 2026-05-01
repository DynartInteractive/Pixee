#include "TaskDockWidget.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "Task.h"
#include "TaskGroup.h"
#include "TaskGroupWidget.h"
#include "TaskItemWidget.h"
#include "TaskManager.h"

TaskDockWidget::TaskDockWidget(TaskManager* manager, QWidget* parent)
    : QDockWidget(tr("Tasks"), parent), _manager(manager) {
    setObjectName("tasksDockWidget");
    setFeatures(QDockWidget::DockWidgetClosable
              | QDockWidget::DockWidgetFloatable
              | QDockWidget::DockWidgetMovable);

    _container = new QWidget();
    _container->setObjectName("tasksDockContainer");
    _containerLayout = new QVBoxLayout(_container);
    _containerLayout->setContentsMargins(2, 2, 2, 2);
    _containerLayout->setSpacing(4);
    _containerLayout->addStretch(1);  // pushes group widgets to the top

    _scrollArea = new QScrollArea();
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setWidget(_container);
    _scrollArea->setFrameShape(QFrame::NoFrame);

    setWidget(_scrollArea);

    connect(_manager, &TaskManager::groupAdded,
            this, &TaskDockWidget::onGroupAdded);
    connect(_manager, &TaskManager::groupRemoved,
            this, &TaskDockWidget::onGroupRemoved);
    connect(_manager, &TaskManager::taskStateChanged,
            this, &TaskDockWidget::onTaskStateChanged);
    connect(_manager, &TaskManager::taskProgress,
            this, &TaskDockWidget::onTaskProgress);
    connect(_manager, &TaskManager::taskQuestionPosed,
            this, &TaskDockWidget::onTaskQuestionPosed);
}

void TaskDockWidget::onGroupAdded(TaskGroup* group) {
    if (!group) return;
    auto* gw = new TaskGroupWidget(group, _container);

    // Wire row clicks → manager. The dock owns the routing here so the
    // group widget can stay decoupled from the manager.
    connect(gw, &TaskGroupWidget::pauseGroupRequested,
            _manager, &TaskManager::pauseGroup);
    connect(gw, &TaskGroupWidget::resumeGroupRequested,
            _manager, &TaskManager::resumeGroup);
    connect(gw, &TaskGroupWidget::stopGroupRequested,
            _manager, &TaskManager::stopGroup);
    connect(gw, &TaskGroupWidget::pauseTaskRequested,
            _manager, &TaskManager::pauseTask);
    connect(gw, &TaskGroupWidget::resumeTaskRequested,
            _manager, &TaskManager::resumeTask);
    connect(gw, &TaskGroupWidget::stopTaskRequested,
            _manager, &TaskManager::stopTask);
    connect(gw, &TaskGroupWidget::answerProvided,
            _manager, &TaskManager::provideAnswer);

    // Insert before the trailing stretch so groups stack at the top.
    const int insertAt = qMax(0, _containerLayout->count() - 1);
    _containerLayout->insertWidget(insertAt, gw);
    _groupWidgets.insert(group->id(), gw);
}

void TaskDockWidget::onGroupRemoved(QUuid groupId) {
    TaskGroupWidget* gw = _groupWidgets.take(groupId);
    if (!gw) return;
    _containerLayout->removeWidget(gw);
    gw->deleteLater();
}

TaskGroupWidget* TaskDockWidget::groupWidgetForTask(const QUuid& taskId) const {
    for (auto it = _groupWidgets.constBegin(); it != _groupWidgets.constEnd(); ++it) {
        if (it.value()->itemFor(taskId)) return it.value();
    }
    return nullptr;
}

void TaskDockWidget::onTaskStateChanged(QUuid taskId, int state) {
    if (auto* gw = groupWidgetForTask(taskId)) gw->onTaskStateChanged(taskId, state);
}

void TaskDockWidget::onTaskProgress(QUuid taskId, int pct) {
    if (auto* gw = groupWidgetForTask(taskId)) gw->onTaskProgress(taskId, pct);
}

void TaskDockWidget::onTaskQuestionPosed(QUuid taskId, int kind, QVariantMap ctx) {
    if (auto* gw = groupWidgetForTask(taskId)) gw->onTaskQuestionPosed(taskId, kind, ctx);
}
