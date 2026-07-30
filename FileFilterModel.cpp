#include "FileFilterModel.h"

#include <QDateTime>

#include "FileItem.h"
#include "FileModel.h"

namespace {
// -1 / 0 / 1 by timestamp. An invalid time (filesystem/platform that doesn't
// report birthTime) sorts as the oldest, so such files cluster predictably
// rather than jumping around.
int compareDateTime(const QDateTime& a, const QDateTime& b) {
    if (a == b) return 0;
    if (!a.isValid()) return -1;
    if (!b.isValid()) return 1;
    return a < b ? -1 : 1;
}
}  // namespace

FileFilterModel::FileFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent}
{
}

bool FileFilterModel::filterAcceptsRow(int row, const QModelIndex &index) const {
    if (_acceptedFileTypes.empty()) {
        return true;
    }
    FileModel* fileModel = qobject_cast<FileModel*>(sourceModel());
    if (!fileModel) {
        return false;
    }
    QModelIndex sourceIndex = fileModel->index(row, 0, index);
    if (!sourceIndex.isValid()) {
        return false;
    }
    FileItem* item = static_cast<FileItem*>(sourceIndex.internalPointer());
    if (item->fileInfo().fileName() == "..") {
        return _showDotDot && item->parent()->parent() != fileModel->rootItem();
    }
    return _acceptedFileTypes.contains(item->fileType());
}

void FileFilterModel::setAcceptedFileTypes(const QList<FileType>& acceptedFileTypes) {
    _acceptedFileTypes = acceptedFileTypes;
}

bool FileFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    FileModel* fileModel = qobject_cast<FileModel*>(sourceModel());
    if (!fileModel) {
        return false;
    }

    // Get the items associated with the indices
    FileItem* leftItem = static_cast<FileItem*>(left.internalPointer());
    FileItem* rightItem = static_cast<FileItem*>(right.internalPointer());

    // ".." always sorts first, even before folder names that ASCII-precede
    // it (e.g. "!something" — yes, those exist on Windows).
    const bool leftIsDotDot  = leftItem->fileInfo().fileName()  == "..";
    const bool rightIsDotDot = rightItem->fileInfo().fileName() == "..";
    if (leftIsDotDot != rightIsDotDot) {
        return leftIsDotDot;
    }

    // Check if both items are directories
    bool leftIsDir = leftItem->fileType() == FileType::Folder;
    bool rightIsDir = rightItem->fileType() == FileType::Folder;

    // If one is a directory and the other isn't, the directory should come
    // first. This is never inverted by the sort direction.
    if (leftIsDir != rightIsDir) {
        return leftIsDir; // Directories come first
    }

    // Within the same group, compare on the chosen key. `c < 0` means left
    // sorts before right in ascending order; the direction flag flips the
    // final result without touching the ".." / folders-first invariants above.
    int c = 0;
    switch (_sortKey) {
    case SortKey::Created:
        c = compareDateTime(leftItem->fileInfo().birthTime(),
                            rightItem->fileInfo().birthTime());
        break;
    case SortKey::Modified:
        c = compareDateTime(leftItem->fileInfo().lastModified(),
                            rightItem->fileInfo().lastModified());
        break;
    case SortKey::Name:
    default:
        break;
    }
    if (c == 0) {
        // Name is the primary key for SortKey::Name and the tiebreak for the
        // date keys (equal timestamps → stable alphabetical order).
        const QString leftName  = fileModel->data(left,  Qt::DisplayRole).toString();
        const QString rightName = fileModel->data(right, Qt::DisplayRole).toString();
        c = FileModel::nameLessThan(leftName, rightName) ? -1
          : FileModel::nameLessThan(rightName, leftName) ?  1 : 0;
    }
    return _sortAscending ? (c < 0) : (c > 0);
}

void FileFilterModel::setShowDotDot(const bool value) {
    _showDotDot = value;
}

void FileFilterModel::setSortKey(SortKey key) {
    if (_sortKey == key) return;
    _sortKey = key;
    invalidate();  // re-sort with the new key
}

void FileFilterModel::setSortAscending(bool ascending) {
    if (_sortAscending == ascending) return;
    _sortAscending = ascending;
    invalidate();  // re-sort with the new direction
}

