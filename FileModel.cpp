#include "FileModel.h"

#include <QDir>
#include <QPainter>
#include <QFileInfo>

#include "Theme.h"
#include "FileItem.h"

FileModel::FileModel(Theme* theme, QObject* parent)
    : QAbstractItemModel(parent) {
    _theme = theme;
    _rootItem = new FileItem(QFileInfo(), FileType::Folder, _theme->pixmap("folder"));
    appendFileItems("/home/gopher/Pictures", _rootItem);
}

FileModel::~FileModel() {
    delete _rootItem;
}

QVariant FileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }
    FileItem* item = static_cast<FileItem*>(index.internalPointer());
    if (role == Qt::DecorationRole) {
        if (item->fileType() == FileType::Folder) {
            QIcon* icon = _theme->icon("folder");
            return QVariant(*icon);
        }
    } else if (role != Qt::DisplayRole) {
        return QVariant();
    }
    return item->fileInfo().filePath().split("/").last();
}

Qt::ItemFlags FileModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant FileModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return QStringLiteral("File Path");

    return QVariant();
}

QModelIndex FileModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    FileItem* parentItem;

    if (!parent.isValid())
        parentItem = _rootItem;
    else
        parentItem = static_cast<FileItem*>(parent.internalPointer());

    FileItem* childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return QModelIndex();
}

QModelIndex FileModel::parent(const QModelIndex& index) const {
    if (!index.isValid())
        return QModelIndex();

    FileItem* childItem = static_cast<FileItem*>(index.internalPointer());
    FileItem* parentItem = childItem->parent();

    if (parentItem == _rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int FileModel::rowCount(const QModelIndex& parent) const {
    FileItem* parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = _rootItem;
    else
        parentItem = static_cast<FileItem*>(parent.internalPointer());

    return parentItem->childCount();
}

int FileModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 1;
}

void FileModel::appendFileItems(const QString& dirPath, FileItem* parent) {

    if (parent->childCount() == 1 && parent->child(0)->fileType() == FileType::Loading) {
        beginRemoveRows(createIndex(parent->row(), 0, parent), 0, 0);
        parent->removeChild(0);
        endRemoveRows();
    } else if (parent != _rootItem){
        /*
        beginRemoveRows(createIndex(parent->row(), 0, parent), 0, parent->childCount());
        for (int i = parent->childCount() - 1; i > -1; i--) {
            parent->removeChild(i);
        }
        endRemoveRows();
        */
        return;
    }


    QDir dir(dirPath);
    QFileInfoList fileList = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot);

    int firstRow = parent->childCount(); // Get the row number where the new rows will be inserted
    int lastRow = firstRow + fileList.size() - 1; // Calculate the last row number

    // Begin inserting rows
    beginInsertRows(createIndex(parent->row(), 0, parent), firstRow, lastRow);

    for (const auto& fileInfo : fileList) {
        FileType fileType = FileType::File;

        if (fileInfo.isDir()) {
            fileType = FileType::Folder;
        } else {
            QString ext = fileInfo.suffix().toLower();
            if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "gif") {
                fileType = FileType::Image;
            }
        }

        QPixmap* pixmap = _theme->pixmap("file");
        if (fileType == FileType::Folder) {
            if (fileInfo.fileName() == "..") {
                pixmap = _theme->pixmap("back");
            } else {
                pixmap = _theme->pixmap("folder");
            }
        } else if (fileType == FileType::Image) {
            pixmap = _theme->pixmap("image");
        }
        // Create and append new FileItem
        FileItem* newItem = new FileItem(fileInfo, fileType, pixmap, parent);
        parent->appendChild(newItem);

        // If it's a folder, append a "Loading..." child item
        if (fileType == FileType::Folder) {
            FileItem* childItem = new FileItem(QFileInfo(), FileType::Loading, pixmap, newItem);
            newItem->appendChild(childItem);
        }
    }

    // End inserting rows
    endInsertRows();
}

FileItem* FileModel::rootItem() const {
    return _rootItem;
}
