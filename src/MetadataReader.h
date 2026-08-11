#ifndef METADATAREADER_H
#define METADATAREADER_H

#include <QAtomicInt>
#include <QObject>
#include <QString>

#include "ImageMetadata.h"

// Reads image metadata off the GUI thread for the info panel. Same
// supersede-on-navigate pattern as ImageLoader / ThumbnailWorker: a `read`
// slot takes a taskVersion snapshot and bails with `aborted` if the live
// abort counter has moved on, so fast prev/next (or rapid selection changes)
// never block on a slow read of a superseded file — important on SMB shares.
//
// The basic fields (dimensions, format, size, mtime) come from Qt and are
// always filled. When PIXEE_HAVE_EXIV2 is defined the reader additionally
// parses EXIF/IPTC/XMP via Exiv2; without it, those stay empty and
// ImageMetadata::exiv2Available is false.
class MetadataReader : public QObject
{
    Q_OBJECT
public:
    explicit MetadataReader(QAtomicInt* abortVersion, QObject* parent = nullptr);

public slots:
    void read(QString path, int taskVersion);

signals:
    void ready(QString path, ImageMetadata metadata);
    void failed(QString path);
    void aborted(QString path);

private:
    bool isAborted(int taskVersion) const;
    QAtomicInt* _abortVersion;
};

#endif // METADATAREADER_H
