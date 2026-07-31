#ifndef FILEOPSHELPERS_H
#define FILEOPSHELPERS_H

#include <QByteArray>
#include <QList>
#include <QString>

class QMimeData;

// Pure-logic helpers for the file-operation pipeline. Extracted from
// FileOpsMenuBuilder.cpp's anonymous namespace and CopyFileTask.cpp so
// the test suite can call them directly (an anon namespace is invisible
// across translation units). No threads, no task-layer dependencies —
// just QString / QFileInfo / QFile / QDir / QMimeData.
namespace FileOpsHelpers {

// True iff `path` is a filesystem root (drive root on Windows, '/' on
// posix). Used to refuse recursive copy / move / delete on a whole drive.
bool isDriveRoot(const QString& path);

// True iff `dest` is the same folder as `source` or a folder under it.
// Refuses pasting / dropping into one's own descendants — the recursive
// folder walk would mkpath new dirs inside the still-iterating source
// tree. Comparison is case-sensitive and uses cleanPath + absolutePath
// so 'D:/foo' matches 'D:\\foo' and 'D:/foo/.' matches 'D:/foo'.
bool destIsSourceOrDescendant(const QString& dest, const QString& source);

// (source file, destination file) pair produced by recursive expansion.
struct Pair {
    QString src;
    QString dst;
};

// Expand `src` (a file or folder) into a list of file-level (src, dst)
// pairs under `destBase`. For folders, replicates the directory tree at
// `destBase / <folderName> / ...` and mkpath's empty subfolders so they
// survive the operation. Symlinks are skipped. Missing source returns
// an empty list (no exception).
QList<Pair> expandToFiles(const QString& src, const QString& destBase);

// Pick a non-clobbering "name (N).ext" for `path` if it already exists.
// Returns the original path when nothing was needed. Gives up after
// 10000 collisions and returns the original path so the open fails
// downstream rather than spinning forever.
QString uniqueRenamedPath(const QString& path);

// Windows clipboard MIME format for the 'Preferred DropEffect' DWORD.
// Other platforms have analogues (gnome-copied-files on Linux); only
// the Windows one is currently consumed.
extern const char* const kDropEffectMime;

// Decodes the 'Preferred DropEffect' DWORD from a clipboard mime-data
// payload. Returns true iff the encoded effect is DROPEFFECT_MOVE (2,
// the Cut semantic). Missing or short payloads return false safely.
bool clipboardSaysCut(const QMimeData* mime);

// Encodes `effect` as the 4-byte little-endian DWORD payload that
// QMimeData::setData(kDropEffectMime, ...) expects.
QByteArray dropEffectBytes(quint32 effect);

}

#endif // FILEOPSHELPERS_H
