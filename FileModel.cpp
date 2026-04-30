#include "FileModel.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QFileInfo>

#include "Theme.h"
#include "FileItem.h"
#include "ThumbnailCache.h"

FileModel::FileModel(Theme* theme, ThumbnailCache* cache, QObject* parent)
    : QAbstractItemModel(parent) {
    _theme = theme;
    _cache = cache;
    _rootItem = new FileItem(QFileInfo(), FileType::Folder, _theme->pixmap("folder"));
    if (_cache) {
        connect(_cache, &ThumbnailCache::thumbnailReady, this, &FileModel::onThumbnailReady);
        connect(_cache, &ThumbnailCache::thumbnailMiss, this, &FileModel::onThumbnailMiss);
        connect(_cache, &ThumbnailCache::thumbnailPending, this, &FileModel::onThumbnailPending);
    }
    populateDrives();
}

void FileModel::populateDrives() {
    const QFileInfoList drives = QDir::drives();
    if (drives.isEmpty()) return;

    beginInsertRows(QModelIndex(), 0, drives.size() - 1);
    QPixmap* folderPixmap = _theme->pixmap("folder");
    for (const QFileInfo& info : drives) {
        FileItem* drive = new FileItem(info, FileType::Folder, folderPixmap, _rootItem);
        _rootItem->appendChild(drive);
        // Loading placeholder so the expand arrow appears before the drive
        // is opened. appendFileItems will replace it on first expansion.
        FileItem* loading = new FileItem(QFileInfo(), FileType::Loading, folderPixmap, drive);
        drive->appendChild(loading);
    }
    endInsertRows();
}

FileModel::~FileModel() {
    delete _rootItem;
}

QVariant FileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }
    FileItem* item = static_cast<FileItem*>(index.internalPointer());
    if (role == ThumbnailRole) {
        const QString path = item->fileInfo().filePath();
        auto it = _thumbnails.constFind(path);
        if (it != _thumbnails.constEnd()) {
            return it.value();
        }
        return QVariant();
    }
    if (role == ThumbnailStateRole) {
        const QString path = item->fileInfo().filePath();
        if (_thumbnails.contains(path)) return StateReady;
        if (_failed.contains(path))     return StateFailed;
        if (_pending.contains(path))    return StatePending;
        return StateIdle;
    }
    if (role == Qt::DecorationRole) {
        if (item->fileType() == FileType::Folder) {
            QIcon* icon = _theme->icon("folder");
            return QVariant(*icon);
        }
    } else if (role != Qt::DisplayRole) {
        return QVariant();
    }
    // Drives (children of the synthetic root) get their drive-letter path as
    // the display name; for everything else strip down to the file name.
    if (item->parent() == _rootItem) {
        return item->fileInfo().filePath();
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
    // The synthetic root holds drives (populated via populateDrives) and
    // never has files of its own, so reject any direct append here.
    if (parent == _rootItem) {
        return;
    }

    if (parent->childCount() == 1 && parent->child(0)->fileType() == FileType::Loading) {
        beginRemoveRows(createIndex(parent->row(), 0, parent), 0, 0);
        parent->removeChild(0);
        endRemoveRows();
    } else {
        // Already populated; don't refresh. (The lazy-load runs once per
        // session — TODO: refresh on demand.)
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
        } else if (fileType == FileType::Image) {
            // Track the item by path so onThumbnailReady can route dataChanged.
            // Subscriptions themselves are driven by FileListView based on viewport.
            _itemsByPath.insert(fileInfo.filePath(), newItem);
        }
    }

    // End inserting rows
    endInsertRows();
}

void FileModel::onThumbnailReady(QString path, QImage image) {
    _pending.remove(path);
    _failed.remove(path);
    _thumbnails.insert(path, image);
    emitDataChangedFor(path);
}

void FileModel::onThumbnailMiss(QString path) {
    _pending.remove(path);
    _thumbnails.remove(path);
    _failed.insert(path);
    emitDataChangedFor(path);
}

void FileModel::onThumbnailPending(QString path) {
    if (_thumbnails.contains(path) || _failed.contains(path)) {
        // Result already known — don't downgrade the state.
        return;
    }
    _pending.insert(path);
    emitDataChangedFor(path);
}

void FileModel::emitDataChangedFor(const QString& path) {
    auto it = _itemsByPath.constFind(path);
    if (it == _itemsByPath.constEnd()) {
        return;
    }
    FileItem* item = it.value();
    const QModelIndex idx = createIndex(item->row(), 0, item);
    emit dataChanged(idx, idx, { Qt::DecorationRole, ThumbnailRole, ThumbnailStateRole });
}

FileItem* FileModel::rootItem() const {
    return _rootItem;
}
