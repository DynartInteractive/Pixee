#include "MoveFileTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr qint64 kChunkSize = 64 * 1024;

QString uniqueRenamedPath(const QString& path) {
    if (!QFile::exists(path)) return path;
    const QFileInfo info(path);
    const QString stem = info.completeBaseName();
    const QString ext = info.suffix();
    const QString dir = info.absolutePath();
    for (int n = 1; n < 10000; ++n) {
        QString candidate = ext.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(stem).arg(n)
                : QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(ext);
        QString full = QDir(dir).filePath(candidate);
        if (!QFile::exists(full)) return full;
    }
    return path;
}
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
    QFile out(_dst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setFailed(tr("Cannot open destination: %1").arg(out.errorString()));
        return false;
    }
    const qint64 total = in.size();
    qint64 written = 0;
    int lastPct = -1;
    while (!in.atEnd()) {
        if (!checkPauseStop()) {
            out.close();
            QFile::remove(_dst);
            return false;
        }
        const QByteArray chunk = in.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (in.error() != QFile::NoError) {
                setFailed(tr("Read error: %1").arg(in.errorString()));
                out.close();
                QFile::remove(_dst);
                return false;
            }
            break;
        }
        if (out.write(chunk) != chunk.size()) {
            setFailed(tr("Write error: %1").arg(out.errorString()));
            out.close();
            QFile::remove(_dst);
            return false;
        }
        written += chunk.size();
        if (total > 0) {
            const int pct = static_cast<int>(written * 95 / total);  // reserve 5% for delete
            if (pct != lastPct) { emitProgress(pct); lastPct = pct; }
        }
    }
    out.close();
    in.close();

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
            if (!QFile::remove(_dst)) {
                setFailed(tr("Cannot remove existing destination: %1").arg(_dst));
                return;
            }
            break;
        case Rename:
            _dst = uniqueRenamedPath(_dst);
            break;
        }
    }

    // Fast path — same-volume rename. Atomic, no progress to report
    // beyond 0 → 100, but the row still shows up in the dock.
    if (QFile::rename(_src, _dst)) {
        emitProgress(100);
        return;
    }

    // Cross-volume or otherwise denied — fall back to copy + delete.
    if (copyAndDelete()) {
        emitProgress(100);
    }
}
