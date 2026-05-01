#ifndef FOLDERCLEANUPTASK_H
#define FOLDERCLEANUPTASK_H

#include <QString>

#include "Task.h"

// Last task in a recursive folder operation.
//
// If `rootPath` is non-empty, walks it bottom-up and rmdirs every empty
// directory — files the user chose 'Skip' on prevent their parent dir
// from being empty, so they're correctly left behind (Move and Delete
// folder cases).
//
// The `additionalRefreshDirs` list is reported via affectedDirs() so the
// folder-refresh debounce in MainWindow knows to refresh the user-visible
// destination root after a Copy / Move folder operation, even though the
// per-file tasks only report deep nested paths.
class FolderCleanupTask : public Task
{
    Q_OBJECT
public:
    FolderCleanupTask(const QString& rootPath,
                      const QStringList& additionalRefreshDirs,
                      TaskGroup* group,
                      QObject* parent = nullptr);

    QString displayName() const override;
    QStringList affectedDirs() const override;

protected:
    void run() override;

private:
    QString _root;                  // empty = no cleanup, just refresh
    QStringList _additionalDirs;
};

#endif // FOLDERCLEANUPTASK_H
