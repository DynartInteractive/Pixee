#include "FileOpsHelpers.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QObject>
#include <QStringList>

namespace FileOpsHelpers {

const char* const kDropEffectMime =
    "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";

bool isDriveRoot(const QString& path) {
    return QFileInfo(path).isRoot();
}

Qt::CaseSensitivity pathCaseSensitivity() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    return QDir::cleanPath(info.absoluteFilePath());
}

bool isSameFile(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    const QFileInfo ia(a);
    const QFileInfo ib(b);
    if (!ia.exists() || !ib.exists()) return false;
    return normalizedPath(a).compare(normalizedPath(b), pathCaseSensitivity()) == 0;
}

bool destIsSourceOrDescendant(const QString& dest, const QString& source) {
    const QString d = normalizedPath(dest);
    const QString s = normalizedPath(source);
    const Qt::CaseSensitivity cs = pathCaseSensitivity();
    if (d.compare(s, cs) == 0) return true;
    // A root ("C:/", "/") already ends in a separator.
    const QString prefix = s.endsWith(QLatin1Char('/')) ? s : s + QLatin1Char('/');
    return d.startsWith(prefix, cs);
}

QString moveAside(const QString& path) {
    const QFileInfo info(path);
    const QString aside = uniqueRenamedPath(
        QDir(info.absolutePath()).filePath(
            QStringLiteral(".%1.pixee-replaced").arg(info.fileName())));
    if (QFile::exists(aside)) return QString();   // uniqueRenamedPath gave up
    if (!QFile::rename(path, aside)) return QString();
    return aside;
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

    // Walk the whole source tree BEFORE creating anything at the
    // destination. If the destination lies inside the source (an alias the
    // descendant guard couldn't see — a mapped drive vs its UNC path), a
    // walk interleaved with mkpath would keep discovering the folders it
    // had just created and nest forever.
    QStringList relDirs;
    QDirIterator dirIt(src,
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        relDirs.append(QDir(src).relativeFilePath(dirIt.next()));
    }

    // Symlinked files are included (QDirIterator doesn't follow links into
    // folders without FollowSymlinks, so only file links come through).
    QDirIterator fileIt(src,
        QDir::Files | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        const QString f = fileIt.next();
        const QString rel = QDir(src).relativeFilePath(f);
        out.append({ f, QDir(folderDest).filePath(rel) });
    }

    // Replicate the directory structure so empty subfolders are preserved
    // at the destination.
    QDir().mkpath(folderDest);
    for (const QString& rel : relDirs) {
        QDir().mkpath(QDir(folderDest).filePath(rel));
    }
    return out;
}

QString fileNameProblem(const QString& name) {
    if (name.isEmpty()) return QObject::tr("Name cannot be empty.");
    if (name.trimmed().isEmpty()) return QObject::tr("Name cannot be only whitespace.");
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return QObject::tr("Reserved name.");
    static const QString kInvalid = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name) {
        if (kInvalid.contains(c))
            return QObject::tr("Name cannot contain: %1").arg(kInvalid);
        if (c.unicode() < 0x20)
            return QObject::tr("Name cannot contain control characters.");
    }
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return QObject::tr("Name cannot end with a dot or a space.");
    // Device names are reserved with any extension ("nul.txt" too).
    const QString stem = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    static const QStringList kReserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    if (kReserved.contains(stem))
        return QObject::tr("\"%1\" is a reserved device name.").arg(stem);
    return QString();
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
