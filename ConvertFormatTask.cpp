#include "ConvertFormatTask.h"

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

ConvertFormatTask::ConvertFormatTask(const QString& sourcePath, const QString& destPath,
                                     const QByteArray& targetFormat, int jpegQuality,
                                     TaskGroup* group, QObject* parent)
    : Task(group, parent),
      _src(sourcePath),
      _dst(destPath),
      _format(targetFormat),
      _jpegQuality(jpegQuality) {}

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
    emitProgress(60);

    QImageWriter writer(_dst, _format);
    if (_format.toLower() == "jpg" || _format.toLower() == "jpeg") {
        writer.setQuality(_jpegQuality);
    }
    if (!writer.write(img)) {
        setFailed(tr("Cannot write: %1").arg(writer.errorString()));
        return;
    }
}
