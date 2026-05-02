#include "TestHelpers.h"

#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QIODevice>
#include <QRandomGenerator>

namespace TestHelpers {

void writeBytes(const QString& path, qint64 size) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    constexpr qint64 kChunk = 64 * 1024;
    QByteArray buf(static_cast<int>(qMin(size, kChunk)), '\0');
    qint64 written = 0;
    while (written < size) {
        const qint64 thisChunk = qMin(kChunk, size - written);
        for (qint64 i = 0; i < thisChunk; ++i) {
            buf[static_cast<int>(i)] =
                static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
        }
        f.write(buf.constData(), thisChunk);
        written += thisChunk;
    }
}

void writeImage(const QString& path, int width, int height, const char* format) {
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(0xFF8080A0);
    QImageWriter w(path, format);
    w.write(img);
}

bool filesEqual(const QString& a, const QString& b) {
    QFile fa(a);
    QFile fb(b);
    if (!fa.open(QIODevice::ReadOnly)) return false;
    if (!fb.open(QIODevice::ReadOnly)) return false;
    if (fa.size() != fb.size()) return false;
    constexpr qint64 kChunk = 64 * 1024;
    while (!fa.atEnd()) {
        if (fa.read(kChunk) != fb.read(kChunk)) return false;
    }
    return true;
}

}
