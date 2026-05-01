#ifndef MOVEFILETASK_H
#define MOVEFILETASK_H

#include <QString>

#include "Task.h"

// Move one file from src → dst. Tries QFile::rename first (cheap, atomic
// on the same volume); on failure falls back to chunked copy + delete-
// source. Surfaces the same DestinationExists conflict prompt as
// CopyFileTask.
class MoveFileTask : public Task
{
    Q_OBJECT
public:
    MoveFileTask(const QString& sourcePath, const QString& destPath,
                 TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;

protected:
    void run() override;

private:
    bool copyAndDelete();    // returns true on success, calls setFailed on failure

    QString _src;
    QString _dst;
};

#endif // MOVEFILETASK_H
