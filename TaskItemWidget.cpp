#include "TaskItemWidget.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "Task.h"

TaskItemWidget::TaskItemWidget(const QUuid& taskId, const QString& name, QWidget* parent)
    : QFrame(parent),
      _taskId(taskId),
      _state(static_cast<int>(Task::Queued)),
      _currentQuestionKind(-1),
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

    _stack = new QStackedWidget();

    // ---- progress page ----
    _progressPage = new QWidget();
    {
        auto* row = new QHBoxLayout(_progressPage);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);

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

        row->addWidget(_progressBar, 1);
        row->addWidget(_pauseButton, 0);
        row->addWidget(_stopButton, 0);
    }
    _stack->addWidget(_progressPage);

    // ---- question page (built lazily on first showQuestion()) ----
    _questionPage = nullptr;

    lay->addWidget(_nameLabel, 0);
    lay->addWidget(_stack, 1);
}

void TaskItemWidget::setProgress(int pct) {
    _progressBar->setValue(pct);
}

void TaskItemWidget::setTaskState(int state) {
    _state = state;
    updatePauseButtonForState();

    if (state == static_cast<int>(Task::AwaitingAnswer)) {
        // Sub-cases set the question via showQuestion() — when the manager's
        // questionPosed signal arrives, that'll swap the page. If state
        // arrived first (race), we keep the progress page; the question
        // signal lands shortly after and triggers the swap then.
    } else if (_currentQuestionKind != -1
               && state != static_cast<int>(Task::AwaitingAnswer)) {
        hideQuestion();
    }

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
    // Don't show hover buttons while the row is in question mode — the
    // question buttons take precedence.
    const bool questionMode = (_currentQuestionKind != -1);
    _pauseButton->setVisible(hovered && !terminal && !questionMode);
    _stopButton->setVisible(hovered && !terminal && !questionMode);
}

void TaskItemWidget::buildQuestionStrip() {
    if (_questionPage) return;

    _questionPage = new QWidget();
    auto* row = new QHBoxLayout(_questionPage);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    struct Btn { const char* label; int answer; bool all; };
    const Btn buttons[] = {
        { QT_TR_NOOP("Skip"),       Task::Skip,      false },
        { QT_TR_NOOP("Skip All"),   Task::Skip,      true  },
        { QT_TR_NOOP("Overwrite"),  Task::Overwrite, false },
        { QT_TR_NOOP("Overwrite All"), Task::Overwrite, true },
        { QT_TR_NOOP("Rename"),     Task::Rename,    false },
        { QT_TR_NOOP("Rename All"), Task::Rename,    true  },
    };
    for (const Btn& b : buttons) {
        auto* btn = new QPushButton(tr(b.label));
        btn->setObjectName("taskQuestionButton");
        btn->setFlat(true);
        btn->setMinimumWidth(0);
        const int answerCopy = b.answer;
        const bool allCopy = b.all;
        connect(btn, &QPushButton::clicked, this, [this, answerCopy, allCopy]() {
            const int kind = _currentQuestionKind;
            // The task is removed from question mode when the manager echoes
            // back a state change — but optimistically we hide here too so
            // the user doesn't double-click a now-stale strip.
            hideQuestion();
            emit answerProvided(_taskId, kind, answerCopy, allCopy);
        });
        row->addWidget(btn);
    }
    _stack->addWidget(_questionPage);
}

void TaskItemWidget::showQuestion(int kind, const QVariantMap& /*context*/) {
    if (!_questionPage) buildQuestionStrip();
    _currentQuestionKind = kind;
    _stack->setCurrentWidget(_questionPage);
    // Hide hover buttons while in question mode.
    _pauseButton->setVisible(false);
    _stopButton->setVisible(false);
}

void TaskItemWidget::hideQuestion() {
    _currentQuestionKind = -1;
    _stack->setCurrentWidget(_progressPage);
}
