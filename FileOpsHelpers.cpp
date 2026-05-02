#include "FileOpsHelpers.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>

namespace FileOpsHelpers {

const char* const kDropEffectMime =
    "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";

bool isDriveRoot(const QString& path) {
    return QFileInfo(path).isRoot();
}

bool destIsSourceOrDescendant(const QString& dest, const QString& source) {
    const QString d = QDir::cleanPath(QDir(dest).absolutePath());
    const QString s = QDir::cleanPath(QDir(source).absolutePath());
    if (d == s) return true;
    return d.startsWith(s + QLatin1Char('/'));
}

QList<Pair> expandToFiles(const QString& src, const QString& destBase) {
    QList<Pair> out;
    const QFileInfo info(src);
    if (!info.exists()) return out;

    if (info.isFile()) {
        out.append({ src, QDir(destBase).filePath(info.fileName()) });
        return out;
    }
    if (!info.isDir()) return out;

    const QString folderName = info.fileName();
    const QString folderDest = QDir(destBase).filePath(folderName);
    QDir().mkpath(folderDest);

    // Replicate the directory structure first so empty subfolders are
    // preserved at the destination.
    QDirIterator dirIt(src,
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        const QString d = dirIt.next();
        const QString rel = QDir(src).relativeFilePath(d);
        QDir().mkpath(QDir(folderDest).filePath(rel));
    }

    QDirIterator fileIt(src,
        QDir::Files | QDir::Hidden | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        const QString f = fileIt.next();
        const QString rel = QDir(src).relativeFilePath(f);
        out.append({ f, QDir(folderDest).filePath(rel) });
    }
    return out;
}

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

bool clipboardSaysCut(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(kDropEffectMime)) return false;
    const QByteArray data = mime->data(kDropEffectMime);
    if (data.size() < 4) return false;
    const quint32 effect = static_cast<quint8>(data.at(0))
                         | (static_cast<quint8>(data.at(1)) << 8)
                         | (static_cast<quint8>(data.at(2)) << 16)
                         | (static_cast<quint8>(data.at(3)) << 24);
    return effect == 2;
}

QByteArray dropEffectBytes(quint32 effect) {
    QByteArray b(4, '\0');
    b[0] = static_cast<char>(effect & 0xFF);
    b[1] = static_cast<char>((effect >> 8) & 0xFF);
    b[2] = static_cast<char>((effect >> 16) & 0xFF);
    b[3] = static_cast<char>((effect >> 24) & 0xFF);
    return b;
}

}
