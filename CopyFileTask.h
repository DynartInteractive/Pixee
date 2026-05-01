#ifndef COPYFILETASK_H
#define COPYFILETASK_H

#include <QString>

#include "Task.h"

// Copy one file from src → dst with chunked I/O so the operation is
// pause/stoppable at chunk boundaries. Phase 3 will add the dest-exists
// conflict handshake; until then the task simply fails when dst already
// exists.
class CopyFileTask : public Task
{
    Q_OBJECT
public:
    CopyFileTask(const QString& sourcePath, const QString& destPath,
                 TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;

protected:
    void run() override;

private:
    QString _src;
    QString _dst;
};

#endif // COPYFILETASK_H
