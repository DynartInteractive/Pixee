#include "CopyFileTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "FileOpsHelpers.h"

namespace {
constexpr qint64 kChunkSize = 64 * 1024;  // matches ImageLoader's chunk size
}

CopyFileTask::CopyFileTask(const QString& sourcePath, const QString& destPath,
                           TaskGroup* group, QObject* parent)
    : Task(group, parent), _src(sourcePath), _dst(destPath) {}

QString CopyFileTask::displayName() const {
    return QObject::tr("Copying %1").arg(QFileInfo(_src).fileName());
}

QStringList CopyFileTask::affectedDirs() const {
    return { QFileInfo(_dst).absolutePath() };
}

void CopyFileTask::run() {
    QFile in(_src);
    if (!in.open(QIODevice::ReadOnly)) {
        setFailed(tr("Cannot open source: %1").arg(in.errorString()));
        return;
    }

    // Ensure the destination's parent directory exists. mkpath is
    // idempotent — cheap when the dir already exists, and lets recursive
    // folder copy work without a separate up-front folder-creation pass.
    QDir().mkpath(QFileInfo(_dst).absolutePath());

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
            // A same-folder paste can hand us a destination that IS the
            // source. "Overwrite" then means replacing the file with itself
            // — a no-op. (isSameFile resolves case and symlinks.)
            if (FileOpsHelpers::isSameFile(_src, _dst)) {
                return;  // execute() marks this Completed — nothing to do
            }
            if (QFileInfo(_dst).isDir()) {
                setFailed(tr("Cannot overwrite a folder: %1").arg(_dst));
                return;
            }
            // No remove here: QSaveFile below replaces the existing file
            // only once the copy is complete, so a failed or cancelled copy
            // leaves the original destination untouched.
            break;
        case Rename:
            _dst = FileOpsHelpers::uniqueRenamedPath(_dst);
            break;
        }
    }

    // QSaveFile writes to a temporary sibling and renames it into place on
    // commit(). Cancel/failure paths just drop the temp file.
    QSaveFile out(_dst);
    if (!out.open(QIODevice::WriteOnly)) {
        setFailed(tr("Cannot open destination: %1").arg(out.errorString()));
        return;
    }

    const qint64 total = in.size();
    qint64 written = 0;
    int lastPct = -1;

    while (!in.atEnd()) {
        if (!checkPauseStop()) {
            out.cancelWriting();  // partial file on cancel — don't leave it behind
            return;
        }
        const QByteArray chunk = in.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (in.error() != QFile::NoError) {
                setFailed(tr("Read error: %1").arg(in.errorString()));
                out.cancelWriting();
                return;
            }
            break;
        }
        const qint64 n = out.write(chunk);
        if (n != chunk.size()) {
            setFailed(tr("Write error: %1").arg(out.errorString()));
            out.cancelWriting();
            return;
        }
        written += n;
        if (total > 0) {
            const int pct = static_cast<int>(written * 100 / total);
            if (pct != lastPct) {
                emitProgress(pct);
                lastPct = pct;
            }
        }
    }
    in.close();
    // commit() flushes the last buffered chunk; a failure there (disk full,
    // share dropped) used to go unnoticed behind an unchecked close().
    if (!out.commit()) {
        setFailed(tr("Write error: %1").arg(out.errorString()));
        return;
    }
}
