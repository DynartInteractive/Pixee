#ifndef SAVEIMAGETASK_H
#define SAVEIMAGETASK_H

#include <QByteArray>
#include <QImage>
#include <QString>

#include "Task.h"

// Write an in-memory QImage to disk in a chosen format. The sibling of
// ConvertFormatTask: where that one decodes a source *file* and re-encodes,
// this one already holds the pixels — the destination for an edited image
// (crop / flip / rotate) that never existed on disk as-is.
//
// The image is held by value; QImage is implicitly shared, so the copy is
// cheap and the worker thread reads it safely without touching the GUI copy.
//
// Output format comes from `format` (e.g. "png", "jpg", "webp"); `quality`
// (0..100) applies to lossy formats only.
//
// When the destination already exists, the DestinationExists conflict prompt
// fires (Skip / Overwrite / Rename), same as the other file tasks. Set
// `overwriteExisting` to skip that prompt and always overwrite — for the
// deliberate File → Save "write back over the original" case, where the user
// already committed to replacing the file and a per-save prompt would just be
// noise.
class SaveImageTask : public Task
{
    Q_OBJECT
public:
    SaveImageTask(const QImage& image, const QString& destPath,
                  const QByteArray& format, int quality,
                  TaskGroup* group, QObject* parent = nullptr,
                  bool overwriteExisting = false);

    QString displayName() const override;
    QStringList affectedDirs() const override;
    QStringList producedPaths() const override { return { _dst }; }

protected:
    void run() override;

private:
    QImage _image;
    QString _dst;
    QByteArray _format;
    int _quality;
    bool _overwriteExisting;
};

#endif // SAVEIMAGETASK_H
