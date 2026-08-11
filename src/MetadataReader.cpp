#include "MetadataReader.h"

#include <QDebug>
#include <QFileInfo>
#include <QImageReader>

#ifdef PIXEE_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

MetadataReader::MetadataReader(QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent), _abortVersion(abortVersion) {}

bool MetadataReader::isAborted(int taskVersion) const {
    return _abortVersion && _abortVersion->loadAcquire() != taskVersion;
}

namespace {

// Fill the always-available fields from Qt. Cheap (header-only) but still
// touches the disk, hence the worker thread.
void readBasic(const QString& path, ImageMetadata& md) {
    const QFileInfo fi(path);
    md.fileBytes = fi.size();
    md.fileModified = fi.lastModified();

    QImageReader reader(path);
    const QSize sz = reader.size();          // reads only the header
    if (sz.isValid()) md.pixelSize = sz;
    const QByteArray fmt = reader.format();
    if (!fmt.isEmpty()) md.format = QString::fromLatin1(fmt).toUpper();
}

#ifdef PIXEE_HAVE_EXIV2
// ---------------------------------------------------------------------------
// Exiv2 parse. Compiled only when the Exiv2 headers/libs are present (see
// Pixee.pro's guarded block and docs/metadata.md). Verified against Exiv2
// 0.28.8; the tests/Metadata round-trip covers the fields extracted here.

QString qstr(const std::string& s) { return QString::fromStdString(s); }

// Human-readable interpreted value for any datum (e.g. "1/250 s", "f/2.8"),
// falling back to the raw string. Works for EXIF / IPTC / XMP — Metadatum::print
// is virtual and takes an optional ExifData* (only EXIF uses it), so the base
// no-arg call is correct for all three containers.
QString interp(const Exiv2::Metadatum& d) {
    try {
        return qstr(d.print());
    } catch (...) {
        try { return qstr(d.toString()); } catch (...) { return QString(); }
    }
}

// EXIF variant: pass the owning ExifData so tags that cross-reference others
// (MakerNote-derived lens, etc.) resolve to their interpreted form.
QString interpExif(const Exiv2::Exifdatum& d, const Exiv2::ExifData& exif) {
    try {
        return qstr(d.print(&exif));
    } catch (...) {
        try { return qstr(d.toString()); } catch (...) { return QString(); }
    }
}

// Look up one EXIF key; empty string if absent.
QString exifVal(Exiv2::ExifData& exif, const char* key) {
    try {
        auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it != exif.end()) return interpExif(*it, exif);
    } catch (...) {}
    return QString();
}

// GPS: three rationals (deg, min, sec) + a hemisphere ref → signed decimal.
bool gpsCoord(Exiv2::ExifData& exif, const char* valueKey, const char* refKey,
              const char* negRefs, double& out) {
    try {
        auto vit = exif.findKey(Exiv2::ExifKey(valueKey));
        auto rit = exif.findKey(Exiv2::ExifKey(refKey));
        if (vit == exif.end() || vit->count() < 3) return false;
        auto r0 = vit->toRational(0);
        auto r1 = vit->toRational(1);
        auto r2 = vit->toRational(2);
        const double deg = r0.second ? double(r0.first) / r0.second : 0.0;
        const double min = r1.second ? double(r1.first) / r1.second : 0.0;
        const double sec = r2.second ? double(r2.first) / r2.second : 0.0;
        double dec = deg + min / 60.0 + sec / 3600.0;
        if (rit != exif.end()) {
            const std::string ref = rit->toString();
            if (!ref.empty() && std::string(negRefs).find(ref[0]) != std::string::npos)
                dec = -dec;
        }
        out = dec;
        return true;
    } catch (...) {
        return false;
    }
}

void readExiv2(const QString& path, ImageMetadata& md) {
    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image) return;
        image->readMetadata();
        md.exiv2Available = true;

        Exiv2::ExifData& exif = image->exifData();
        if (!exif.empty()) {
            md.cameraMake   = exifVal(exif, "Exif.Image.Make");
            md.cameraModel  = exifVal(exif, "Exif.Image.Model");
            md.lens         = exifVal(exif, "Exif.Photo.LensModel");
            md.dateTaken    = exifVal(exif, "Exif.Photo.DateTimeOriginal");
            if (md.dateTaken.isEmpty())
                md.dateTaken = exifVal(exif, "Exif.Image.DateTime");
            md.exposureTime = exifVal(exif, "Exif.Photo.ExposureTime");
            md.fNumber      = exifVal(exif, "Exif.Photo.FNumber");
            md.isoSpeed     = exifVal(exif, "Exif.Photo.ISOSpeedRatings");
            md.focalLength  = exifVal(exif, "Exif.Photo.FocalLength");

            try {
                auto oit = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
                if (oit != exif.end()) md.orientation = int(oit->toInt64());
            } catch (...) {}

            double lat = 0.0, lon = 0.0;
            const bool haveLat = gpsCoord(
                exif, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", "Ss", lat);
            const bool haveLon = gpsCoord(
                exif, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", "Ww", lon);
            if (haveLat && haveLon) {
                md.hasGps = true;
                md.gpsLat = lat;
                md.gpsLon = lon;
                md.gpsText = QStringLiteral("%1, %2")
                                 .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
            }

            for (const auto& d : exif)
                md.allTags.append({qstr(d.key()), interpExif(d, exif)});
        }

        Exiv2::IptcData& iptc = image->iptcData();
        for (const auto& d : iptc)
            md.allTags.append({qstr(d.key()), interp(d)});

        Exiv2::XmpData& xmp = image->xmpData();
        for (const auto& d : xmp)
            md.allTags.append({qstr(d.key()), interp(d)});
    } catch (const std::exception& e) {
        qWarning() << "MetadataReader: Exiv2 failed for" << path << ":" << e.what();
    } catch (...) {
        qWarning() << "MetadataReader: Exiv2 failed for" << path;
    }
}
#endif  // PIXEE_HAVE_EXIV2

}  // namespace

void MetadataReader::read(QString path, int taskVersion) {
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

    ImageMetadata md;
    md.path = path;
    readBasic(path, md);

    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }

#ifdef PIXEE_HAVE_EXIV2
    readExiv2(path, md);
    if (isAborted(taskVersion)) {
        emit aborted(path);
        return;
    }
#endif

    emit ready(path, md);
}
