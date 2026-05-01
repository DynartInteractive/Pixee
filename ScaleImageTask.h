#ifndef SCALEIMAGETASK_H
#define SCALEIMAGETASK_H

#include <QString>

#include "Task.h"

// Decode an image, scale it so its longest edge equals targetLongestEdge
// (preserving aspect), and write it back. EXIF orientation is honoured on
// read (matches ImageLoader). Honours the DestinationExists handshake.
class ScaleImageTask : public Task
{
    Q_OBJECT
public:
    ScaleImageTask(const QString& sourcePath, const QString& destPath,
                   int targetLongestEdge, int jpegQuality,
                   TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;

protected:
    void run() override;

private:
    QString _src;
    QString _dst;
    int _longestEdge;
    int _jpegQuality;
};

#endif // SCALEIMAGETASK_H
