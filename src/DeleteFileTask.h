#ifndef DELETEFILETASK_H
#define DELETEFILETASK_H

#include <QString>

#include "Task.h"

// Delete one file or folder (folders are removed as a single unit, so the
// trash gets one entry per top-level item rather than N per-file
// entries). Single-shot but Task-shaped so it shows up in the dock.
// Confirmation dialogs live in MainWindow — by the time a DeleteFileTask
// is constructed, the user has already approved the operation.
class DeleteFileTask : public Task
{
    Q_OBJECT
public:
    // toTrash=true (default) tries OS Recycle Bin / XDG Trash first and
    // falls back to a hard remove if the volume has no trash (network
    // shares). toTrash=false hard-deletes unconditionally — used by the
    // external-Move-out cleanup, where the file moved away and a
    // recoverable trash copy would contradict the Move semantic.
    DeleteFileTask(const QString& path, TaskGroup* group,
                   bool toTrash = true, QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;

protected:
    void run() override;

private:
    QString _path;
    bool _toTrash;
};

#endif // DELETEFILETASK_H
