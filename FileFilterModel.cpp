#include "FileFilterModel.h"

#include "FileItem.h"
#include "FileModel.h"

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

    // Check if both items are directories
    bool leftIsDir = leftItem->fileType() == FileType::Folder;
    bool rightIsDir = rightItem->fileType() == FileType::Folder;

    // If one is a directory and the other isn't, the directory should come first
    if (leftIsDir != rightIsDir) {
        return leftIsDir; // Directories come first
    }

    // Otherwise, sort alphabetically
    QString leftName = fileModel->data(left, Qt::DisplayRole).toString();
    QString rightName = fileModel->data(right, Qt::DisplayRole).toString();
    return QString::localeAwareCompare(leftName.toLower(), rightName.toLower()) < 0;
}

void FileFilterModel::setShowDotDot(const bool value) {
    _showDotDot = value;
}

