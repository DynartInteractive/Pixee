#include "DeleteFileTask.h"

#include <QFile>
#include <QFileInfo>

DeleteFileTask::DeleteFileTask(const QString& path, TaskGroup* group, QObject* parent)
    : Task(group, parent), _path(path) {}

QString DeleteFileTask::displayName() const {
    return QObject::tr("Deleting %1").arg(QFileInfo(_path).fileName());
}

QStringList DeleteFileTask::affectedDirs() const {
    return { QFileInfo(_path).absolutePath() };
}

void DeleteFileTask::run() {
    if (!checkPauseStop()) return;
    if (!QFile::exists(_path)) {
        // Treat as a successful no-op — the user wanted it gone, it's gone.
        emitProgress(100);
        return;
    }
    QFile f(_path);
    if (!f.remove()) {
        setFailed(tr("Cannot delete: %1").arg(f.errorString()));
        return;
    }
    emitProgress(100);
}
