#include "FolderSuggester.h"

#include <QDir>

#include <algorithm>

#include "FileModel.h"

FolderSuggester::FolderSuggester(QAtomicInt* abortVersion, QObject* parent)
    : QObject(parent), _abortVersion(abortVersion) {}

bool FolderSuggester::isAborted(int taskVersion) const {
    return _abortVersion && _abortVersion->loadAcquire() != taskVersion;
}

QStringList FolderSuggester::filterAndSort(const QStringList& names,
                                           const QString& prefix) {
    QStringList out;
    out.reserve(qMin(names.size(), kMaxSuggestions));
    for (const QString& name : names) {
        if (prefix.isEmpty() || name.startsWith(prefix, Qt::CaseInsensitive)) {
            out.append(name);
        }
    }
    // FileModel::nameLessThan is the same comparator the folder tree and the
    // file list sort by, so the popup lists folders in the order the user is
    // about to see them in.
    std::sort(out.begin(), out.end(), FileModel::nameLessThan);
    if (out.size() > kMaxSuggestions) out = out.mid(0, kMaxSuggestions);
    return out;
}

void FolderSuggester::suggest(QString dirPath, QString prefix, int taskVersion) {
    // Check before the read as well as after: crossing several separators
    // quickly can queue a few of these, and the stale ones should not touch
    // the disk at all.
    if (isAborted(taskVersion)) return;

    const QDir dir(dirPath);
    if (!dir.exists()) {
        // Still reply, so the popup hides rather than lingering on the last
        // directory's contents while the user types a path that isn't there.
        emit suggested(dirPath, prefix, QStringList(), taskVersion);
        return;
    }

    // Hidden folders stay out (no QDir::Hidden) to match the browser. Symlinks
    // are left in: to the user they are folders like any other.
    const QStringList names =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
    if (isAborted(taskVersion)) return;

    emit suggested(dirPath, prefix, filterAndSort(names, prefix), taskVersion);
}
