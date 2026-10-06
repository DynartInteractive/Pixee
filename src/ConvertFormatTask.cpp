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
            // Nothing is removed up front: writeImage replaces the existing
            // file only once the new one is complete. That also makes
            // dst == src (re-encode in place — the Save As dialog's default
            // target) safe: the source is fully decoded before anything is
            // written.
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
    emitProgress(60);

    QString error;
    if (!ImageFormats::writeImage(img, _dst, _format, _quality, &error)) {
        setFailed(error);
        return;
    }
}
