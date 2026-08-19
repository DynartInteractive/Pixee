#include "SaveImageTask.h"

#include <QFile>
#include <QFileInfo>
#include <QImageWriter>

#include "FileOpsHelpers.h"

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
            // We hold the pixels in memory, so removing the destination first
            // is safe — a failed write can't lose the (only-in-memory) edit's
            // source, but it does destroy the old file, so bail if remove fails.
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
    emitProgress(30);

    QImageWriter writer(_dst, _format);
    const QByteArray f = _format.toLower();
    if (f == "jpg" || f == "jpeg" || f == "webp") {
        writer.setQuality(_quality);
    }
    if (!writer.write(_image)) {
        setFailed(tr("Cannot write: %1").arg(writer.errorString()));
        return;
    }

    emitProgress(100);
}
