#include "TaskItemWidget.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QToolButton>

#include "Task.h"

TaskItemWidget::TaskItemWidget(const QUuid& taskId, const QString& name, QWidget* parent)
    : QFrame(parent),
      _taskId(taskId),
      _state(static_cast<int>(Task::Queued)),
      _hovered(false) {
    setObjectName("taskItemWidget");
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_Hover, true);  // fires HoverEnter/Leave even when child buttons get the mouse

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 2, 4, 2);
    lay->setSpacing(6);

    _nameLabel = new QLabel(name);
    _nameLabel->setObjectName("taskItemName");
    _nameLabel->setMinimumWidth(80);

    _progressBar = new QProgressBar();
    _progressBar->setObjectName("taskItemProgress");
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _progressBar->setTextVisible(false);
    _progressBar->setFixedHeight(8);

    _pauseButton = new QToolButton();
    _pauseButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPause));
    _pauseButton->setAutoRaise(true);
    _pauseButton->setToolTip(tr("Pause"));
    _pauseButton->setVisible(false);
    connect(_pauseButton, &QToolButton::clicked, this, [this]() {
        if (_state == static_cast<int>(Task::Paused)) emit resumeRequested(_taskId);
        else                                          emit pauseRequested(_taskId);
    });

    _stopButton = new QToolButton();
    _stopButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaStop));
    _stopButton->setAutoRaise(true);
    _stopButton->setToolTip(tr("Stop"));
    _stopButton->setVisible(false);
    connect(_stopButton, &QToolButton::clicked, this, [this]() {
        emit stopRequested(_taskId);
    });

    lay->addWidget(_nameLabel, 0);
    lay->addWidget(_progressBar, 1);
    lay->addWidget(_pauseButton, 0);
    lay->addWidget(_stopButton, 0);
}

void TaskItemWidget::setProgress(int pct) {
    _progressBar->setValue(pct);
}

void TaskItemWidget::setTaskState(int state) {
    _state = state;
    updatePauseButtonForState();
    // Disable hover buttons once the task is in a terminal state — there's
    // nothing to pause or stop anymore.
    const bool terminal = (state == Task::Completed
                        || state == Task::Failed
                        || state == Task::Aborted
                        || state == Task::Skipped);
    if (terminal) {
        _pauseButton->setEnabled(false);
        _stopButton->setEnabled(false);
    }
}

void TaskItemWidget::updatePauseButtonForState() {
    if (_state == static_cast<int>(Task::Paused)) {
        _pauseButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        _pauseButton->setToolTip(tr("Resume"));
    } else {
        _pauseButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPause));
        _pauseButton->setToolTip(tr("Pause"));
    }
}

bool TaskItemWidget::event(QEvent* e) {
    switch (e->type()) {
    case QEvent::HoverEnter:
        if (!_hovered) { _hovered = true; updateButtonsForHover(true); }
        break;
    case QEvent::HoverLeave:
        if (_hovered) { _hovered = false; updateButtonsForHover(false); }
        break;
    default:
        break;
    }
    return QFrame::event(e);
}

void TaskItemWidget::updateButtonsForHover(bool hovered) {
    const bool terminal = (_state == Task::Completed
                        || _state == Task::Failed
                        || _state == Task::Aborted
                        || _state == Task::Skipped);
    _pauseButton->setVisible(hovered && !terminal);
    _stopButton->setVisible(hovered && !terminal);
}
