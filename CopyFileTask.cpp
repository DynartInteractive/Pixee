#include "CopyFileTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr qint64 kChunkSize = 64 * 1024;  // matches ImageLoader's chunk size

// Pick a non-clobbering "name (N).ext" for `path` if it already exists.
// Returns the original path when nothing was needed.
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
    return path;  // give up after 10000 collisions; let the open fail downstream
}
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

    QFile out(_dst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setFailed(tr("Cannot open destination: %1").arg(out.errorString()));
        return;
    }

    const qint64 total = in.size();
    qint64 written = 0;
    int lastPct = -1;

    while (!in.atEnd()) {
        if (!checkPauseStop()) {
            out.close();
            QFile::remove(_dst);  // partial file on cancel — don't leave it behind
            return;
        }
        const QByteArray chunk = in.read(kChunkSize);
        if (chunk.isEmpty()) {
            if (in.error() != QFile::NoError) {
                setFailed(tr("Read error: %1").arg(in.errorString()));
                out.close();
                QFile::remove(_dst);
                return;
            }
            break;
        }
        const qint64 n = out.write(chunk);
        if (n != chunk.size()) {
            setFailed(tr("Write error: %1").arg(out.errorString()));
            out.close();
            QFile::remove(_dst);
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
    out.close();
    in.close();
}
