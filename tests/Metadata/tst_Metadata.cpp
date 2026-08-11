#include <QtTest>

#include <QAtomicInt>
#include <QImage>
#include <QImageWriter>
#include <QMap>
#include <QSize>
#include <QTemporaryDir>

#include "ImageMetadata.h"
#include "MetadataReader.h"

#ifdef PIXEE_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

// Exercises MetadataReader end to end. The reader's read() slot emits ready()
// synchronously on a direct (same-thread) connection, so we can drive it
// inline and capture the struct without an event loop.
//
// basics() always runs (Qt-only). exifRoundTrip() only compiles/runs in a
// PIXEE_HAVE_EXIV2 build: it writes known EXIF with Exiv2, then asserts the
// reader parses it back.
class tst_Metadata : public QObject
{
    Q_OBJECT

    ImageMetadata readSync(const QString& path) {
        QAtomicInt version(1);
        MetadataReader reader(&version);
        ImageMetadata got;
        bool ready = false;
        connect(&reader, &MetadataReader::ready, this,
                [&](const QString&, const ImageMetadata& md) { got = md; ready = true; });
        reader.read(path, 1);  // direct call → ready() fires inline
        if (!ready) return {};
        return got;
    }

    static QString writeJpeg(const QString& path, int w, int h) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        QImageWriter writer(path, "jpeg");
        writer.write(img);
        return path;
    }

private slots:
    void basics() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p = dir.filePath("basic.jpg");
        writeJpeg(p, 120, 80);

        const ImageMetadata md = readSync(p);
        QCOMPARE(md.path, p);
        QCOMPARE(md.format, QStringLiteral("JPEG"));
        QCOMPARE(md.pixelSize, QSize(120, 80));
        QVERIFY(md.fileBytes > 0);
        QVERIFY(md.fileModified.isValid());
    }

    void pngTextChunks() {
        // ComfyUI/A1111 embed generation data as PNG tEXt chunks; the reader
        // must surface them via Qt regardless of the Exiv2 backend.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p = dir.filePath("gen.png");
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(Qt::black);
        QImageWriter writer(p, "png");
        writer.setText("prompt", "{\"3\":{\"class_type\":\"KSampler\"}}");
        writer.setText("workflow", "{\"nodes\":[]}");
        QVERIFY(writer.write(img));

        const ImageMetadata md = readSync(p);
        QCOMPARE(md.format, QStringLiteral("PNG"));
        QMap<QString, QString> got;
        for (const auto& kv : md.textChunks) got.insert(kv.first, kv.second);
        QVERIFY2(got.contains("prompt"), "prompt text chunk missing");
        QVERIFY(got.value("prompt").contains("KSampler"));
        QVERIFY2(got.contains("workflow"), "workflow text chunk missing");
    }

    void missingFileYieldsEmptyBasics() {
        // A non-existent path still emits ready() with empty basics (no crash).
        const ImageMetadata md = readSync(QStringLiteral("Z:/nope/absent.jpg"));
        QCOMPARE(md.fileBytes, qint64(0));
        QVERIFY(!md.pixelSize.isValid());
    }

#ifdef PIXEE_HAVE_EXIV2
    void exifRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p = dir.filePath("exif.jpg");
        writeJpeg(p, 200, 100);

        // Stamp known EXIF (incl. GPS) into the file with Exiv2.
        {
            auto image = Exiv2::ImageFactory::open(p.toStdString());
            QVERIFY(image.get() != nullptr);
            image->readMetadata();
            Exiv2::ExifData& ed = image->exifData();
            ed["Exif.Image.Make"]             = "PixeeCam";
            ed["Exif.Image.Model"]            = "Model X";
            ed["Exif.Image.Orientation"]      = uint16_t(6);
            ed["Exif.Photo.DateTimeOriginal"] = "2024:01:02 03:04:05";
            ed["Exif.Photo.FNumber"]          = "28/10";       // f/2.8
            ed["Exif.Photo.ISOSpeedRatings"]  = uint16_t(400);
            ed["Exif.GPSInfo.GPSLatitudeRef"]  = "N";
            ed["Exif.GPSInfo.GPSLatitude"]     = "47/1 29/1 5268/100";  // 47.498°
            ed["Exif.GPSInfo.GPSLongitudeRef"] = "E";
            ed["Exif.GPSInfo.GPSLongitude"]    = "19/1 2/1 2544/100";   // 19.040°
            image->writeMetadata();
        }

        const ImageMetadata md = readSync(p);
        QVERIFY(md.exiv2Available);
        QVERIFY2(md.cameraMake.contains("PixeeCam"), qPrintable(md.cameraMake));
        QVERIFY2(md.cameraModel.contains("Model X"), qPrintable(md.cameraModel));
        QCOMPARE(md.orientation, 6);
        QVERIFY(!md.fNumber.isEmpty());
        QVERIFY(!md.isoSpeed.isEmpty());
        QVERIFY(!md.dateTaken.isEmpty());
        QVERIFY(md.hasGps);
        QVERIFY2(qAbs(md.gpsLat - 47.498) < 0.01,
                 qPrintable(QString::number(md.gpsLat, 'f', 6)));
        QVERIFY2(qAbs(md.gpsLon - 19.040) < 0.01,
                 qPrintable(QString::number(md.gpsLon, 'f', 6)));
        QVERIFY(!md.allTags.isEmpty());
    }

    void noExifStillReadsBasics() {
        // A freshly-written JPEG with no EXIF: exiv2 ran, but the rich fields
        // stay empty and the basics are still correct.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p = dir.filePath("plain.jpg");
        writeJpeg(p, 64, 48);

        const ImageMetadata md = readSync(p);
        QVERIFY(md.exiv2Available);
        QCOMPARE(md.pixelSize, QSize(64, 48));
        QVERIFY(md.cameraMake.isEmpty());
        QVERIFY(!md.hasGps);
    }
#endif  // PIXEE_HAVE_EXIV2
};

QTEST_MAIN(tst_Metadata)
#include "tst_Metadata.moc"
