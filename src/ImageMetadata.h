#ifndef IMAGEMETADATA_H
#define IMAGEMETADATA_H

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QSize>
#include <QString>

// Parsed metadata for one image, produced off-thread by MetadataReader and
// handed to MetadataPanel for display. Deliberately Exiv2-free: the reader
// converts Exiv2 types into these plain fields inside its .cpp, so the struct
// crosses the queued signal/slot boundary (registered as a metatype) without
// dragging Exiv2 headers into the rest of the build.
//
// The "basic" block is always populated from Qt (QImageReader + QFileInfo).
// The "rich" block is filled only when Exiv2 is compiled in (PIXEE_HAVE_EXIV2)
// AND the file actually carries that metadata; `exiv2Available` says whether
// the rich parse ran at all, so the panel can distinguish "no Exiv2 in this
// build" from "this file has no EXIF".
struct ImageMetadata {
    QString path;

    // --- Basic (Qt-derived, always present) -------------------------------
    QSize     pixelSize;        // decoded dimensions
    QString   format;           // "JPEG", "PNG", ...
    qint64    fileBytes = 0;
    QDateTime fileModified;

    // --- Rich (Exiv2; empty strings when absent) --------------------------
    QString cameraMake;
    QString cameraModel;
    QString lens;
    QString dateTaken;          // preformatted for display
    QString exposureTime;       // "1/250 s"
    QString fNumber;            // "f/2.8"
    QString isoSpeed;
    QString focalLength;        // "50 mm"
    int     orientation = 0;    // EXIF Orientation 1..8; 0 = unknown/none

    bool    hasGps = false;
    double  gpsLat = 0.0;       // signed decimal degrees (N/E positive)
    double  gpsLon = 0.0;
    QString gpsText;            // preformatted "47.497913, 19.040236"

    // PNG/embedded text chunks read via Qt (tEXt/zTXt/iTXt), independent of
    // Exiv2 — this is where AI-tool generation data lives: ComfyUI writes
    // `prompt` + `workflow`, Automatic1111 writes `parameters`. Also standard
    // keys like Description/Software/Comment. Populated in every build.
    QList<QPair<QString, QString>> textChunks;

    // Full tag dump for the power-user expandable view: (key, value) pairs
    // like ("Exif.Image.Make", "Canon"). Empty in the basic-only build.
    QList<QPair<QString, QString>> allTags;

    // True when the Exiv2 parse actually executed for this file (regardless
    // of whether it found anything). False in a build without Exiv2.
    bool exiv2Available = false;
};

Q_DECLARE_METATYPE(ImageMetadata)

#endif // IMAGEMETADATA_H
