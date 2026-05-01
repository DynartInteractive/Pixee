#include "CopyFileTask.h"

#include <QFile>
#include <QFileInfo>

namespace {
constexpr qint64 kChunkSize = 64 * 1024;  // matches ImageLoader's chunk size
}

CopyFileTask::CopyFileTask(const QString& sourcePath, const QString& destPath,
                           TaskGroup* group, QObject* parent)
    : Task(group, parent), _src(sourcePath), _dst(destPath) {}

QString CopyFileTask::displayName() const {
    return QObject::tr("Copying %1").arg(QFileInfo(_src).fileName());
}

void CopyFileTask::run() {
    QFile in(_src);
    if (!in.open(QIODevice::ReadOnly)) {
        setFailed(tr("Cannot open source: %1").arg(in.errorString()));
        return;
    }

    // Phase 3 will replace this hard fail with resolveOrAsk() on
    // DestinationExists. Until then, refuse to clobber.
    if (QFile::exists(_dst)) {
        setFailed(tr("Destination already exists: %1").arg(_dst));
        return;
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
