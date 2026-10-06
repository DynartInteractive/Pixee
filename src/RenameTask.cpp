#include "RenameTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "FileOpsHelpers.h"

RenameTask::RenameTask(const QString& sourcePath, const QString& destPath,
                       TaskGroup* group, QObject* parent)
    : Task(group, parent), _src(sourcePath), _dst(destPath) {}

QString RenameTask::displayName() const {
    return QObject::tr("Renaming %1 → %2")
        .arg(QFileInfo(_src).fileName(), QFileInfo(_dst).fileName());
}

QStringList RenameTask::affectedDirs() const {
    // Batch renames stay in one folder, but a caller could target another;
    // list both parents so either folder refreshes.
    const QString srcDir = QFileInfo(_src).absolutePath();
    const QString dstDir = QFileInfo(_dst).absolutePath();
    if (srcDir == dstDir) return { srcDir };
    return { srcDir, dstDir };
}

void RenameTask::run() {
    if (!checkPauseStop()) return;

    // Name unchanged — nothing to do (the plan should filter these out, but a
    // stray no-op must not fail or prompt).
    if (_src == _dst) {
        emitProgress(100);
        return;
    }

    // A case-only rename ("IMG.jpg" → "img.jpg") on a case-insensitive
    // filesystem: the "existing" destination is the source itself. It must
    // not reach the conflict prompt, whose Overwrite used to delete the file.
    // QFile::rename handles a case change directly.
    if (FileOpsHelpers::isSameFile(_src, _dst)) {
        if (QFile::rename(_src, _dst)) {
            emitProgress(100);
            return;
        }
        setFailed(tr("Cannot rename %1 to %2")
                      .arg(QFileInfo(_src).fileName(), QFileInfo(_dst).fileName()));
        return;
    }

    // Overwrite moves the existing file aside instead of deleting it, so a
    // failed rename can put it back.
    QString aside;
    if (QFile::exists(_dst)) {
        QVariantMap ctx;
        ctx.insert("src", _src);
        ctx.insert("dst", _dst);
        const ConflictAnswer answer = resolveOrAsk(DestinationExists, ctx);
        if (isStopRequested()) return;
        switch (answer) {
        case Skip:
            setSkipped();
            return;
        case Overwrite:
            if (QFileInfo(_dst).isDir()) {
                setFailed(tr("Cannot overwrite a folder: %1").arg(_dst));
                return;
            }
            aside = FileOpsHelpers::moveAside(_dst);
            if (aside.isEmpty()) {
                setFailed(tr("Cannot remove existing file: %1").arg(_dst));
                return;
            }
            break;
        case Rename:
            _dst = FileOpsHelpers::uniqueRenamedPath(_dst);
            break;
        }
    }

    // QDir::rename: no copy fallback (QFile::rename would silently copy +
    // delete across volumes), and it renames folders as well as files.
    if (QDir().rename(_src, _dst)) {
        if (!aside.isEmpty()) QFile::remove(aside);
        emitProgress(100);
        return;
    }
    if (!aside.isEmpty()) QFile::rename(aside, _dst);
    setFailed(tr("Cannot rename %1 to %2")
                  .arg(QFileInfo(_src).fileName(), QFileInfo(_dst).fileName()));
}
