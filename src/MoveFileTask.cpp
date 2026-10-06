#include "MoveFileTask.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "FileOpsHelpers.h"

namespace {
constexpr qint64 kChunkSize = 64 * 1024;
}

MoveFileTask::MoveFileTask(const QString& sourcePath, const QString& destPath,
                           TaskGroup* group, QObject* parent)
    : Task(group, parent), _src(sourcePath), _dst(destPath) {}

QString MoveFileTask::displayName() const {
    return QObject::tr("Moving %1").arg(QFileInfo(_src).fileName());
}

QStringList MoveFileTask::affectedDirs() const {
    const QString srcDir = QFileInfo(_src).absolutePath();
    const QString dstDir = QFileInfo(_dst).absolutePath();
    if (srcDir == dstDir) return { srcDir };  // pure rename
    return { srcDir, dstDir };
}

bool MoveFileTask::copyAndDelete() {
    QFile in(_src);
    if (!in.open(QIODevice::ReadOnly)) {
        setFailed(tr("Cannot open source: %1").arg(in.errorString()));
        return false;
    }
    const QDateTime srcModified = QFileInfo(_src).lastModified();
    // QSaveFile: the destination only appears once every byte is written
    // and flushed, and commit() reports a failed final flush — the source
    // must never be removed on the strength of an incomplete copy.
    QSaveFile out(_dst);
    if (!out.open(QIODevice::WriteOnly)) {
        setFailed(tr("Cannot open destination: %1").arg(out.errorString()));
        return false;
    }
    const qint64 total = in.size();
    qint64 written = 0;
    int lastPct = -1;
    while (!in.atEnd()) {
        if (!checkPauseStop()) {
            out.cancelWriting();
            return false;
        }
        const QByteArray chunk = in.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (in.error() != QFile::NoError) {
                setFailed(tr("Read error: %1").arg(in.errorString()));
                out.cancelWriting();
                return false;
            }
            break;
        }
        if (out.write(chunk) != chunk.size()) {
            setFailed(tr("Write error: %1").arg(out.errorString()));
            out.cancelWriting();
            return false;
        }
        written += chunk.size();
        if (total > 0) {
            const int pct = static_cast<int>(written * 95 / total);  // reserve 5% for delete
            if (pct != lastPct) { emitProgress(pct); lastPct = pct; }
        }
    }
    in.close();
    if (!out.commit()) {
        setFailed(tr("Write error: %1").arg(out.errorString()));
        return false;
    }
    // Belt and braces before the irreversible step: the copy must be whole.
    if (QFileInfo(_dst).size() != total) {
        QFile::remove(_dst);
        setFailed(tr("Copy of %1 is incomplete; source kept").arg(_src));
        return false;
    }
    // A move keeps the file's date (the fast rename path does too), so the
    // file doesn't jump around under Sort by → Modified.
    {
        QFile stamp(_dst);
        if (stamp.open(QIODevice::ReadWrite)) {
            stamp.setFileTime(srcModified, QFileDevice::FileModificationTime);
        }
    }

    if (!QFile::remove(_src)) {
        setFailed(tr("Copied to %1 but cannot remove source: %2").arg(_dst, _src));
        return false;
    }
    return true;
}

void MoveFileTask::run() {
    // Ensure the destination's parent directory exists so recursive
    // folder moves don't trip on a missing nested target. Idempotent.
    QDir().mkpath(QFileInfo(_dst).absolutePath());

    // Destination is the source itself — a move into the folder the file
    // already lives in. Never let this reach the conflict prompt: its
    // Overwrite used to delete the file. A case-only difference on a
    // case-insensitive filesystem is a real rename (QFile::rename handles it).
    if (FileOpsHelpers::isSameFile(_src, _dst)) {
        if (_src.compare(_dst, Qt::CaseSensitive) == 0) {
            setSkipped();   // nothing to move
            return;
        }
        if (!QFile::rename(_src, _dst)) {
            setFailed(tr("Cannot rename %1").arg(_src));
            return;
        }
        emitProgress(100);
        return;
    }

    // Overwrite moves the existing destination aside rather than deleting
    // it, so a move that then fails can put it back.
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
                setFailed(tr("Cannot replace existing destination: %1").arg(_dst));
                return;
            }
            break;
        case Rename:
            _dst = FileOpsHelpers::uniqueRenamedPath(_dst);
            break;
        }
    }

    // Fast path — same-volume rename. Atomic, no progress to report
    // beyond 0 → 100, but the row still shows up in the dock.
    // Otherwise (cross-volume or denied) fall back to copy + delete.
    // QDir::rename, not QFile::rename: the latter silently falls back to
    // its own copy + remove-source when the native rename fails, with no
    // progress, no cancel, and no check of the final flush.
    const bool moved = QDir().rename(_src, _dst) || copyAndDelete();

    if (!aside.isEmpty()) {
        if (moved) {
            QFile::remove(aside);
        } else {
            // Undo: drop whatever reached the destination (only possible
            // when the copy landed but the source couldn't be removed — the
            // source is still there), and restore the original.
            if (QFile::exists(_dst)) QFile::remove(_dst);
            QFile::rename(aside, _dst);
        }
    }
    if (moved) emitProgress(100);
}
