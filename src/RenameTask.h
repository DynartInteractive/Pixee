#ifndef RENAMETASK_H
#define RENAMETASK_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "Task.h"

// Rename one file in place (src → dst). A batch rename enqueues one of these
// per file into a single group. Like MoveFileTask it surfaces the
// DestinationExists conflict prompt and reports the rename via movedPaths() so
// the cached thumbnail is repointed rather than regenerated — but it only ever
// does an in-place QFile::rename (no cross-volume copy fallback), because a
// rename never crosses folders.
class RenameTask : public Task
{
    Q_OBJECT
public:
    RenameTask(const QString& sourcePath, const QString& destPath,
               TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;
    QStringList producedPaths() const override { return { _dst }; }
    QList<QPair<QString, QString>> movedPaths() const override { return { { _src, _dst } }; }

protected:
    void run() override;

private:
    QString _src;
    QString _dst;
};

#endif // RENAMETASK_H
