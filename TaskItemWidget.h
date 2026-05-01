#ifndef TASKITEMWIDGET_H
#define TASKITEMWIDGET_H

#include <QFrame>
#include <QString>
#include <QUuid>
#include <QVariantMap>

class QLabel;
class QProgressBar;
class QToolButton;

// One row in the task dock — name + progress bar, with pause / stop buttons
// that appear on hover. Phase 3 will swap the progress strip for a question
// button strip when the task is awaiting an answer.
class TaskItemWidget : public QFrame
{
    Q_OBJECT
public:
    TaskItemWidget(const QUuid& taskId, const QString& name, QWidget* parent = nullptr);

    QUuid taskId() const { return _taskId; }
    void setProgress(int pct);
    // state matches Task::State.
    void setTaskState(int state);

signals:
    void pauseRequested(QUuid taskId);
    void resumeRequested(QUuid taskId);
    void stopRequested(QUuid taskId);

protected:
    bool event(QEvent* e) override;

private:
    void updateButtonsForHover(bool hovered);
    void updatePauseButtonForState();

    QUuid _taskId;
    QLabel* _nameLabel;
    QProgressBar* _progressBar;
    QToolButton* _pauseButton;
    QToolButton* _stopButton;
    int _state;
    bool _hovered;
};

#endif // TASKITEMWIDGET_H
