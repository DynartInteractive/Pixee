#include "FolderCleanupTask.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

FolderCleanupTask::FolderCleanupTask(const QString& rootPath,
                                     const QStringList& additionalRefreshDirs,
                                     TaskGroup* group, QObject* parent)
    : Task(group, parent),
      _root(rootPath),
      _additionalDirs(additionalRefreshDirs) {}

QString FolderCleanupTask::displayName() const {
    return QObject::tr("Finalizing batch");
}

QStringList FolderCleanupTask::affectedDirs() const {
    QStringList r;
    if (!_root.isEmpty()) {
        // The folder we're cleaning up itself disappears; the parent gains
        // a missing entry. Both warrant a refresh.
        const QFileInfo info(_root);
        r.append(info.absolutePath());
        r.append(_root);
    }
    r.append(_additionalDirs);
    return r;
}

void FolderCleanupTask::run() {
    if (_root.isEmpty() || !QFileInfo(_root).isDir()) {
        // Pure 'refresh marker' mode (Copy folder), or the source root
        // was already removed by other means. Just emit 100% and let
        // affectedDirs() drive the auto-refresh.
        emitProgress(100);
        return;
    }

    // Collect every subdirectory under root, deepest first. QDirIterator
    // gives us a flat list; sorting by length-descending puts deeper
    // paths before their parents, so each rmdir attempt sees an actually-
    // empty dir if every file beneath it was successfully moved/deleted.
    QStringList dirs;
    QDirIterator it(_root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (!checkPauseStop()) return;
        dirs.append(it.next());
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });

    for (const QString& d : dirs) {
        if (!checkPauseStop()) return;
        QDir(d).rmdir(".");  // succeeds only if empty; otherwise silently no-ops
    }
    // Finally try to remove the root itself.
    QDir(_root).rmdir(".");

    emitProgress(100);
}
