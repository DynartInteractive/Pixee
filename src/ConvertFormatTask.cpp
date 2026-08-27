#include "ConvertFormatTask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>

#include "FileOpsHelpers.h"
#include "ImageFormats.h"

ConvertFormatTask::ConvertFormatTask(const QString& sourcePath, const QString& destPath,
                                     const QByteArray& targetFormat, int quality,
                                     TaskGroup* group, QObject* parent)
    : Task(group, parent),
      _src(sourcePath),
      _dst(destPath),
      _format(targetFormat),
      _quality(quality) {}

QString ConvertFormatTask::displayName() const {
    return QObject::tr("Converting %1 → %2")
            .arg(QFileInfo(_src).fileName(), QString::fromLatin1(_format));
}

QStringList ConvertFormatTask::affectedDirs() const {
    return { QFileInfo(_dst).absolutePath() };
}

void ConvertFormatTask::run() {
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
    emitProgress(60);

    QImageWriter writer(_dst, _format);
    if (ImageFormats::isLossy(QString::fromLatin1(_format))) {
        writer.setQuality(_quality);
    }
    if (!writer.write(img)) {
        setFailed(tr("Cannot write: %1").arg(writer.errorString()));
        return;
    }
}
