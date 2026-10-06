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
    if (!info.exists() && !info.isSymLink()) {   // a dangling link still exists
        // Treat as a successful no-op — the user wanted it gone, it's gone.
        emitProgress(100);
        return;
    }
    // OS trash / recycle bin if requested. Cross-platform: Windows Recycle
    // Bin, Linux XDG Trash, macOS Trash. Returns false on volumes without a
    // trash (typical for network shares / SMB mounts). The user confirmed a
    // recoverable "Delete", so do NOT silently fall through to a permanent
    // delete — fail instead and point at the explicit permanent delete.
    if (_toTrash) {
        if (QFile::moveToTrash(_path)) {
            emitProgress(100);
            return;
        }
        setFailed(tr("Could not move \"%1\" to the trash (this drive may not "
                     "have one). Use Shift+Delete to delete it permanently.")
                      .arg(info.fileName()));
        return;
    }
    // A symlink / junction: remove the link itself. removeRecursively()
    // through a link to a folder would delete the target's contents.
    if (info.isSymLink() || info.isJunction()) {
        if (QFile::remove(_path) || QDir().rmdir(info.absoluteFilePath())) {
            emitProgress(100);
            return;
        }
        setFailed(tr("Cannot delete link: %1").arg(_path));
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
