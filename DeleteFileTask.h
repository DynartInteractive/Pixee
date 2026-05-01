#ifndef DELETEFILETASK_H
#define DELETEFILETASK_H

#include <QString>

#include "Task.h"

// Delete one file. Single-shot but Task-shaped so it shows up in the dock.
// Confirmation dialogs live in MainWindow — by the time a DeleteFileTask
// is constructed, the user has already approved the operation.
class DeleteFileTask : public Task
{
    Q_OBJECT
public:
    DeleteFileTask(const QString& path, TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;

protected:
    void run() override;

private:
    QString _path;
};

#endif // DELETEFILETASK_H
