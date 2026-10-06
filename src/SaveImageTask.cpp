#include "SaveImageTask.h"

#include <QFile>
#include <QFileInfo>
#include <QImageWriter>

#include "FileOpsHelpers.h"
#include "ImageFormats.h"

SaveImageTask::SaveImageTask(const QImage& image, const QString& destPath,
                             const QByteArray& format, int quality,
                             TaskGroup* group, QObject* parent,
                             bool overwriteExisting)
    : Task(group, parent),
      _image(image),
      _dst(destPath),
      _format(format),
      _quality(quality),
      _overwriteExisting(overwriteExisting) {}

QString SaveImageTask::displayName() const {
    return QObject::tr("Saving %1").arg(QFileInfo(_dst).fileName());
}

QStringList SaveImageTask::affectedDirs() const {
    return { QFileInfo(_dst).absolutePath() };
}

void SaveImageTask::run() {
    if (_image.isNull()) {
        setFailed(tr("Nothing to save (empty image)."));
        return;
    }
    // Refuse up front if no plugin can encode the format (GIF, SVG, ... are
    // readable but not writable) — before any prompt or file is touched.
    if (!ImageFormats::canWrite(_format)) {
        setFailed(tr("Saving as %1 is not supported")
                      .arg(QString::fromLatin1(_format).toUpper()));
        return;
    }

    if (QFile::exists(_dst)) {
        // The deliberate "Save over the original" case bypasses the prompt;
        // otherwise ask, exactly like ConvertFormatTask.
        ConflictAnswer answer = Overwrite;
        if (!_overwriteExisting) {
            QVariantMap ctx;
            ctx.insert("dst", _dst);
            answer = resolveOrAsk(DestinationExists, ctx);
            if (isStopRequested()) return;
        }
        switch (answer) {
        case Skip:
            setSkipped();
            return;
        case Overwrite:
            // Nothing removed up front: writeImage replaces the existing file
            // only once the new bytes are completely on disk, so a failed
            // write (unsupported format, full disk) leaves the original —
            // usually the very file the edit was made from — intact.
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
    emitProgress(30);

    QString error;
    if (!ImageFormats::writeImage(_image, _dst, _format, _quality, &error)) {
        setFailed(error);
        return;
    }

    emitProgress(100);
}
