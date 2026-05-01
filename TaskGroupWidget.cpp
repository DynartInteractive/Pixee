#include "TaskGroupWidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Task.h"
#include "TaskGroup.h"
#include "TaskItemWidget.h"

TaskGroupWidget::TaskGroupWidget(TaskGroup* group, QWidget* parent)
    : QFrame(parent),
      _groupId(group->id()),
      _expanded(true),
      _groupPaused(false) {
    setObjectName("taskGroupWidget");
    setFrameShape(QFrame::StyledPanel);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(0);

    // ---- header ----
    auto* header = new QFrame(this);
    header->setObjectName("taskGroupHeader");
    auto* hh = new QHBoxLayout(header);
    hh->setContentsMargins(4, 2, 4, 2);
    hh->setSpacing(4);

    _toggleButton = new QToolButton(header);
    _toggleButton->setArrowType(Qt::DownArrow);
    _toggleButton->setAutoRaise(true);
    _toggleButton->setToolTip(tr("Collapse / expand"));
    connect(_toggleButton, &QToolButton::clicked, this, [this]() {
        _expanded = !_expanded;
        _body->setVisible(_expanded);
        _toggleButton->setArrowType(_expanded ? Qt::DownArrow : Qt::RightArrow);
    });

    _nameLabel = new QLabel(group->displayName(), header);
    _nameLabel->setObjectName("taskGroupName");

    _aggregate = new QProgressBar(header);
    _aggregate->setObjectName("taskGroupAggregate");
    _aggregate->setRange(0, 100);
    _aggregate->setValue(0);
    _aggregate->setTextVisible(false);
    _aggregate->setFixedHeight(8);
    _aggregate->setMinimumWidth(60);

    _pauseGroupButton = new QToolButton(header);
    _pauseGroupButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPause));
    _pauseGroupButton->setAutoRaise(true);
    _pauseGroupButton->setToolTip(tr("Pause all tasks in group"));
    connect(_pauseGroupButton, &QToolButton::clicked, this, [this]() {
        if (_groupPaused) emit resumeGroupRequested(_groupId);
        else              emit pauseGroupRequested(_groupId);
    });

    _stopGroupButton = new QToolButton(header);
    _stopGroupButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaStop));
    _stopGroupButton->setAutoRaise(true);
    _stopGroupButton->setToolTip(tr("Stop all tasks in group"));
    connect(_stopGroupButton, &QToolButton::clicked, this, [this]() {
        emit stopGroupRequested(_groupId);
    });

    hh->addWidget(_toggleButton, 0);
    hh->addWidget(_nameLabel, 1);
    hh->addWidget(_aggregate, 1);
    hh->addWidget(_pauseGroupButton, 0);
    hh->addWidget(_stopGroupButton, 0);
    outer->addWidget(header);

    // ---- body ----
    _body = new QFrame(this);
    _body->setObjectName("taskGroupBody");
    _bodyLayout = new QVBoxLayout(_body);
    _bodyLayout->setContentsMargins(0, 0, 0, 0);
    _bodyLayout->setSpacing(0);
    outer->addWidget(_body);

    // Build a row per task at construction time. New tasks aren't added to a
    // group after enqueue in the current API.
    for (Task* t : group->tasks()) {
        auto* row = new TaskItemWidget(t->id(), t->displayName(), _body);
        connect(row, &TaskItemWidget::pauseRequested,
                this, &TaskGroupWidget::pauseTaskRequested);
        connect(row, &TaskItemWidget::resumeRequested,
                this, &TaskGroupWidget::resumeTaskRequested);
        connect(row, &TaskItemWidget::stopRequested,
                this, &TaskGroupWidget::stopTaskRequested);
        connect(row, &TaskItemWidget::answerProvided,
                this, &TaskGroupWidget::answerProvided);
        _bodyLayout->addWidget(row);
        _items.insert(t->id(), row);
        _progress.insert(t->id(), 0);
        _states.insert(t->id(), static_cast<int>(Task::Queued));
    }
}

void TaskGroupWidget::onTaskQuestionPosed(const QUuid& taskId, int kind, const QVariantMap& ctx) {
    if (auto* row = _items.value(taskId, nullptr)) {
        row->showQuestion(kind, ctx);
    }
}

void TaskGroupWidget::onTaskProgress(const QUuid& taskId, int pct) {
    if (auto* row = _items.value(taskId, nullptr)) {
        row->setProgress(pct);
    }
    _progress.insert(taskId, pct);
    rebuildAggregateProgress();
}

void TaskGroupWidget::onTaskStateChanged(const QUuid& taskId, int state) {
    if (auto* row = _items.value(taskId, nullptr)) {
        row->setTaskState(state);
    }
    _states.insert(taskId, state);
    // If the group is in paused state but no tasks are still pause-able,
    // sync the icon back to a play icon.
    updateGroupPauseButton();
}

void TaskGroupWidget::rebuildAggregateProgress() {
    if (_progress.isEmpty()) {
        _aggregate->setValue(0);
        return;
    }
    int total = 0;
    int count = 0;
    for (auto it = _progress.constBegin(); it != _progress.constEnd(); ++it) {
        total += it.value();
        ++count;
    }
    _aggregate->setValue(count > 0 ? total / count : 0);
}

void TaskGroupWidget::updateGroupPauseButton() {
    bool anyPaused = false;
    for (auto it = _states.constBegin(); it != _states.constEnd(); ++it) {
        if (it.value() == static_cast<int>(Task::Paused)) { anyPaused = true; break; }
    }
    _groupPaused = anyPaused;
    _pauseGroupButton->setIcon(QApplication::style()->standardIcon(
        anyPaused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    _pauseGroupButton->setToolTip(anyPaused ? tr("Resume all tasks in group")
                                            : tr("Pause all tasks in group"));
}
