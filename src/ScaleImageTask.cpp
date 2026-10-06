#include "ScaleImageTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>

#include "FileOpsHelpers.h"
#include "ImageFormats.h"

ScaleImageTask::ScaleImageTask(const QString& sourcePath, const QString& destPath,
                               int targetLongestEdge, int quality,
                               TaskGroup* group, QObject* parent)
    : Task(group, parent),
      _src(sourcePath),
      _dst(destPath),
      _longestEdge(targetLongestEdge),
      _quality(quality) {}

QString ScaleImageTask::displayName() const {
    return QObject::tr("Scaling %1").arg(QFileInfo(_src).fileName());
}

QStringList ScaleImageTask::affectedDirs() const {
    return { QFileInfo(_dst).absolutePath() };
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
            // Nothing removed up front — writeImage replaces the existing
            // file only once the new one is complete (and dst == src is safe:
            // the source is decoded before anything is written).
            if (QFileInfo(_dst).isDir()) {
                setFailed(tr("Cannot overwrite a folder: %1").arg(_dst));
                return;
            }
            break;
        case Rename:
            _dst = FileOpsHelpers::uniqueRenamedPath(_dst);
            break;
        }
    }

    if (!checkPauseStop()) return;
    emitProgress(10);

    // Scoped: the reader holds the source open until destroyed, and Windows
    // won't let writeImage replace a file that is still open (dst == src).
    QImage img;
    {
        QImageReader reader(_src);
        reader.setAutoTransform(true);
        img = reader.read();
        if (img.isNull()) {
            setFailed(tr("Cannot decode: %1").arg(reader.errorString()));
            return;
        }
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

    // Name the format explicitly rather than letting QImageWriter infer it
    // from the destination suffix. Two reasons: the suffix may be an alias
    // no plugin claims (.jfif is JPEG, and inferring would fail the write
    // with "Unsupported image format"), and a writer constructed from a
    // bare filename reports an empty format(), so testing it to decide on
    // setQuality() never matched and JPEG quality was silently ignored.
    const QByteArray format = ImageFormats::writerFormatFor(QFileInfo(_dst).suffix());

    QString error;
    if (!ImageFormats::writeImage(img, _dst, format, _quality, &error)) {
        setFailed(error);
        return;
    }
}
