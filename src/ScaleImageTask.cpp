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

    // Name the format explicitly rather than letting QImageWriter infer it
    // from the destination suffix. Two reasons: the suffix may be an alias
    // no plugin claims (.jfif is JPEG, and inferring would fail the write
    // with "Unsupported image format"), and a writer constructed from a
    // bare filename reports an empty format(), so testing it to decide on
    // setQuality() never matched and JPEG quality was silently ignored.
    const QString suffix = QFileInfo(_dst).suffix().toLower();
    QByteArray format = ImageFormats::aliasedFormat(suffix);
    if (format.isEmpty()) format = suffix.toLatin1();

    QImageWriter writer(_dst, format);
    if (format == "jpg" || format == "jpeg") {
        writer.setQuality(_jpegQuality);
    }
    if (!writer.write(img)) {
        setFailed(tr("Cannot write: %1").arg(writer.errorString()));
        return;
    }
}
