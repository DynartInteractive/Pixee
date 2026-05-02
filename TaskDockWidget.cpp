#include "TaskDockWidget.h"

#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
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

    // Header: "Clear all finished" button + thin separator. Stays at the
    // top of the scroll container; new group widgets are inserted below
    // it (above the trailing stretch).
    _clearAllFinishedButton = new QPushButton(tr("Clear all finished"));
    _clearAllFinishedButton->setObjectName("clearAllFinishedButton");
    _clearAllFinishedButton->setFlat(true);
    _clearAllFinishedButton->setCursor(Qt::PointingHandCursor);
    _clearAllFinishedButton->setEnabled(false);
    connect(_clearAllFinishedButton, &QPushButton::clicked,
            _manager, &TaskManager::clearAllFinished);
    _containerLayout->addWidget(_clearAllFinishedButton);

    auto* headerSep = new QFrame();
    headerSep->setObjectName("tasksDockHeaderSep");
    headerSep->setFrameShape(QFrame::NoFrame);
    headerSep->setFixedHeight(1);
    _containerLayout->addWidget(headerSep);

    _containerLayout->addStretch(1);  // pushes group widgets toward the top

    _scrollArea = new QScrollArea();
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setWidget(_container);
    _scrollArea->setFrameShape(QFrame::NoFrame);

    setWidget(_scrollArea);

    connect(_manager, &TaskManager::groupAdded,
            this, &TaskDockWidget::onGroupAdded);
    connect(_manager, &TaskManager::groupRemoved,
            this, &TaskDockWidget::onGroupRemoved);
    connect(_manager, &TaskManager::groupFinished,
            this, &TaskDockWidget::onGroupFinished);
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
    connect(gw, &TaskGroupWidget::clearGroupRequested,
            _manager, &TaskManager::clearGroup);

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
    updateClearAllFinishedButton();
}

void TaskDockWidget::onGroupFinished(QUuid /*groupId*/) {
    updateClearAllFinishedButton();
}

void TaskDockWidget::updateClearAllFinishedButton() {
    _clearAllFinishedButton->setEnabled(_manager->hasFinishedGroups());
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
    auto* gw = groupWidgetForTask(taskId);
    if (!gw) return;
    gw->expand();                            // pop the group open
    gw->onTaskQuestionPosed(taskId, kind, ctx);

    // Defer scroll + flash so the layout settles after expand() and the
    // dock's just-shown widgets have a chance to render their baseline
    // state. Without this:
    //  1. ensureWidgetVisible reads stale geometry — when other groups
    //     above are already expanded, the row scrolls to the wrong
    //     vertical position (or not at all).
    //  2. The first 'blinking=true' QSS polish on a freshly-shown dock
    //     races the initial paint, so the user sees one flash instead
    //     of two.
    // We also force a layout->activate() pass before scrolling — the
    // posted layout-update events from expand() may not have fired by
    // the time singleShot(0) does, so we run them ourselves.
    if (auto* row = gw->itemFor(taskId)) {
        QPointer<TaskItemWidget> rowPtr(row);
        QPointer<QScrollArea> areaPtr(_scrollArea);
        QTimer::singleShot(0, this, [rowPtr, areaPtr]() {
            if (!rowPtr) return;
            if (areaPtr && areaPtr->widget() && areaPtr->widget()->layout()) {
                areaPtr->widget()->layout()->activate();
            }
            if (areaPtr) areaPtr->ensureWidgetVisible(rowPtr, 0, 32);
            rowPtr->flashAttention();
        });
    }
}
