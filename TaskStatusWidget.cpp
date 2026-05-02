#include "TaskStatusWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>

#include "TaskManager.h"

TaskStatusWidget::TaskStatusWidget(TaskManager* manager, QWidget* parent)
    : QWidget(parent), _manager(manager) {
    setObjectName("taskStatusWidget");
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    _bar = new QProgressBar(this);
    _bar->setObjectName("taskStatusBar");
    _bar->setRange(0, 100);
    _bar->setValue(0);
    _bar->setTextVisible(false);
    _bar->setFixedHeight(8);
    _bar->setFixedWidth(120);

    _label = new QLabel(QStringLiteral("0 / 0"), this);
    _label->setObjectName("taskStatusLabel");

    layout->addWidget(_bar);
    layout->addWidget(_label);

    // Throttle: 100 ms is plenty fine for a status-bar indicator and
    // keeps the bar from repainting per chunk-progress emit.
    _refreshTimer.setSingleShot(true);
    _refreshTimer.setInterval(100);
    connect(&_refreshTimer, &QTimer::timeout, this, &TaskStatusWidget::refresh);

    auto kick = [this]() {
        if (!_refreshTimer.isActive()) _refreshTimer.start();
    };
    connect(_manager, &TaskManager::groupAdded, this, kick);
    connect(_manager, &TaskManager::groupFinished, this, kick);
    connect(_manager, &TaskManager::groupRemoved, this, kick);
    connect(_manager, &TaskManager::taskStateChanged, this, kick);
    connect(_manager, &TaskManager::taskProgress, this, kick);

    setVisible(false);                 // hidden until there's work
}

void TaskStatusWidget::refresh() {
    const int total = _manager->totalTaskCount();
    const int done = _manager->terminalTaskCount();
    const int pct = _manager->aggregateProgressPercent();
    const bool active = _manager->hasActiveTasks();

    _bar->setValue(pct);
    _label->setText(QStringLiteral("%1 / %2").arg(done).arg(total));
    setToolTip(tr("%1 of %2 tasks done — click to show / hide the tasks dock")
                   .arg(done).arg(total));
    setVisible(active);
}

void TaskStatusWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit toggleDockRequested();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
