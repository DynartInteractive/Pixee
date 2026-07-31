#include "DeleteFileTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

DeleteFileTask::DeleteFileTask(const QString& path, TaskGroup* group,
                               bool toTrash, QObject* parent)
    : Task(group, parent), _path(path), _toTrash(toTrash) {}

QString DeleteFileTask::displayName() const {
    return QObject::tr("Deleting %1").arg(QFileInfo(_path).fileName());
}

QStringList DeleteFileTask::affectedDirs() const {
    return { QFileInfo(_path).absolutePath() };
}

void DeleteFileTask::run() {
    if (!checkPauseStop()) return;
    const QFileInfo info(_path);
    if (!info.exists()) {
        // Treat as a successful no-op — the user wanted it gone, it's gone.
        emitProgress(100);
        return;
    }
    // OS trash / recycle bin first if requested. Cross-platform: Windows
    // Recycle Bin, Linux XDG Trash, macOS Trash. Returns false on volumes
    // without a trash (typical for network shares / SMB mounts) — fall
    // through to hard delete in that case so the user's request still
    // goes through. Same path as the explicit hard-delete (toTrash=false)
    // used by the external Move-out source cleanup.
    if (_toTrash && QFile::moveToTrash(_path)) {
        emitProgress(100);
        return;
    }
    if (info.isDir()) {
        QDir d(_path);
        if (!d.removeRecursively()) {
            setFailed(tr("Cannot delete folder: %1").arg(_path));
            return;
        }
    } else {
        QFile f(_path);
        if (!f.remove()) {
            setFailed(tr("Cannot delete: %1").arg(f.errorString()));
            return;
        }
    }
    emitProgress(100);
}
