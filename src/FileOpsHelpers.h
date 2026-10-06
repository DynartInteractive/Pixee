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

// How paths compare on the platform's usual filesystems: case-insensitive
// on Windows and macOS (NTFS / default APFS), case-sensitive elsewhere.
Qt::CaseSensitivity pathCaseSensitivity();

// Absolute, cleaned form of `path` with symlinks / junctions resolved when
// the path exists (canonicalFilePath), falling back to cleanPath of the
// absolute path when it doesn't.
QString normalizedPath(const QString& path);

// True iff `a` and `b` name the same existing file or folder — compared on
// normalizedPath() with the platform's case rule, so "a.jpg" vs "A.jpg" on
// Windows, or a path through a symlink, count as the same file. Both must
// exist. Tasks use this before an Overwrite so they never delete the very
// file they were asked to keep.
bool isSameFile(const QString& a, const QString& b);

// True iff `dest` is the same folder as `source` or a folder under it.
// Refuses pasting / dropping into one's own descendants — the recursive
// folder walk would mkpath new dirs inside the still-iterating source
// tree. Compares normalizedPath() forms with the platform's case rule,
// so 'D:/foo' matches 'D:\\foo', 'D:/foo/.' and (on Windows) 'd:/FOO'.
bool destIsSourceOrDescendant(const QString& dest, const QString& source);

// For an Overwrite that has to remove the destination before the new file
// can take its place (a rename/move can't replace an existing file):
// renames `path` to a hidden sibling and returns that sibling's path, so
// the caller can delete it once the replacement has landed — or rename it
// back if the operation fails. Returns an empty string if the rename fails.
QString moveAside(const QString& path);

// (source file, destination file) pair produced by recursive expansion.
struct Pair {
    QString src;
    QString dst;
};

// Expand `src` (a file or folder) into a list of file-level (src, dst)
// pairs under `destBase`. For folders, replicates the directory tree at
// `destBase / <folderName> / ...` and mkpath's empty subfolders so they
// survive the operation. The source tree is fully enumerated before any
// destination folder is created, so a destination inside the source can't
// feed the walk. Symlinked folders are not descended into (and not
// replicated); symlinked files are included. Missing source returns an
// empty list (no exception).
QList<Pair> expandToFiles(const QString& src, const QString& destBase);

// Why `name` can't be used as a single file / folder name, or an empty
// string if it can. Rejects empty / whitespace-only names, "." and "..",
// path separators and the other characters Windows forbids (a ':' would
// silently create an NTFS alternate data stream), control characters,
// trailing dots / spaces (Windows strips them, so the name on disk would
// differ from the one asked for) and reserved device names (CON, NUL,
// COM1, ...). Applied on every platform so names stay portable to the
// Windows shares this app is mostly used on.
QString fileNameProblem(const QString& name);

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
