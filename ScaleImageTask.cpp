#include "ScaleImageTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>

namespace {
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

ScaleImageTask::ScaleImageTask(const QString& sourcePath, const QString& destPath,
                               int targetLongestEdge, int jpegQuality,
                               TaskGroup* group, QObject* parent)
    : Task(group, parent),
      _src(sourcePath),
      _dst(destPath),
      _longestEdge(targetLongestEdge),
      _jpegQuality(jpegQuality) {}

QString ScaleImageTask::displayName() const {
    return QObject::tr("Scaling %1").arg(QFileInfo(_src).fileName());
}

void ScaleImageTask::run() {
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

    if (!checkPauseStop()) return;
    emitProgress(10);

    QImageReader reader(_src);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        setFailed(tr("Cannot decode: %1").arg(reader.errorString()));
        return;
    }

    if (!checkPauseStop()) return;
    emitProgress(45);

    const int w = img.width();
    const int h = img.height();
    const int longest = qMax(w, h);
    if (longest > _longestEdge && _longestEdge > 0) {
        // Smooth downscale; keep aspect.
        img = img.scaled(_longestEdge, _longestEdge,
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (!checkPauseStop()) return;
    emitProgress(75);

    QImageWriter writer(_dst);
    if (writer.format().toLower() == "jpg" || writer.format().toLower() == "jpeg") {
        writer.setQuality(_jpegQuality);
    }
    if (!writer.write(img)) {
        setFailed(tr("Cannot write: %1").arg(writer.errorString()));
        return;
    }
}
