#include "CopyFileTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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
            if (!QFile::remove(_dst)) {
                setFailed(tr("Cannot remove existing destination: %1").arg(_dst));
                return;
            }
            break;
        case Rename:
            _dst = FileOpsHelpers::uniqueRenamedPath(_dst);
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
