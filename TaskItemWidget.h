#ifndef TASKITEMWIDGET_H
#define TASKITEMWIDGET_H

#include <QFrame>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

class QLabel;
class QProgressBar;
class QStackedWidget;
class QToolButton;
class QWidget;

// One row in the task dock — name + progress bar, with pause / stop buttons
// that appear on hover. When the task is awaiting an answer (Phase 3) the
// progress strip is swapped for a question strip with six buttons:
// Skip / Skip All / Overwrite / Overwrite All / Rename / Rename All.
class TaskItemWidget : public QFrame
{
    Q_OBJECT
public:
    TaskItemWidget(const QUuid& taskId, const QString& name, QWidget* parent = nullptr);

    QUuid taskId() const { return _taskId; }
    void setProgress(int pct);
    // state matches Task::State.
    void setTaskState(int state);

    // Show the inline question strip; restored to the progress strip on
    // any non-AwaitingAnswer state change.
    void showQuestion(int kind, const QVariantMap& context);

    // Briefly flashes the row's background twice to draw the user's eye
    // — used when the task posts a conflict question and the dock has
    // just been popped open. No-op if a flash is already in flight.
    void flashAttention();

signals:
    void pauseRequested(QUuid taskId);
    void resumeRequested(QUuid taskId);
    void stopRequested(QUuid taskId);
    // answer is Task::ConflictAnswer cast to int; kind is Task::QuestionKind.
    void answerProvided(QUuid taskId, int kind, int answer, bool applyToGroup);

protected:
    bool event(QEvent* e) override;

private:
    void updateButtonsForHover(bool hovered);
    void updatePauseButtonForState();
    void buildQuestionStrip();
    void hideQuestion();

    QUuid _taskId;
    QLabel* _nameLabel;
    QStackedWidget* _stack;
    QWidget* _progressPage;
    QWidget* _questionPage;
    QProgressBar* _progressBar;
    QToolButton* _pauseButton;
    QToolButton* _stopButton;
    int _state;
    int _currentQuestionKind;
    bool _hovered;
    // Drives the two-flash attention pulse: ticks every 150 ms,
    // toggles the QSS-driven `blinking` property, stops after 4 ticks.
    QTimer _flashTimer;
    int _flashStep = 0;
};

#endif // TASKITEMWIDGET_H
