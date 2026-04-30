#include "FileModel.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QFileInfo>

#include "Config.h"
#include "Theme.h"
#include "FileItem.h"
#include "FolderEnumerator.h"
#include "ThumbnailCache.h"

FileModel::FileModel(Config* config, Theme* theme, ThumbnailCache* cache, QObject* parent)
    : QAbstractItemModel(parent) {
    _theme = theme;
    _cache = cache;
    _rootItem = new FileItem(QFileInfo(), FileType::Folder, _theme->pixmap("folder"));
    // Build a quick-lookup set from whatever Qt's plugins reported supporting
    // — keeps the image classifier honest as the Qt build changes.
    if (config) {
        for (const QString& ext : config->imageExtensions()) {
            _imageExtensions.insert(ext.toLower());
        }
    }
    if (_cache) {
        connect(_cache, &ThumbnailCache::thumbnailReady, this, &FileModel::onThumbnailReady);
        connect(_cache, &ThumbnailCache::thumbnailMiss, this, &FileModel::onThumbnailMiss);
        connect(_cache, &ThumbnailCache::thumbnailPending, this, &FileModel::onThumbnailPending);
    }

    // Worker thread for off-GUI directory enumeration. The enumerator is
    // auto-deleted when the thread stops; requestEnumerate dispatches via
    // a queued signal so the directory read runs off the GUI thread.
    _enumerator = new FolderEnumerator();
    _enumerator->moveToThread(&_enumThread);
    connect(&_enumThread, &QThread::finished, _enumerator, &QObject::deleteLater);
    connect(this, &FileModel::requestEnumerateSignal, _enumerator, &FolderEnumerator::enumerate);
    connect(_enumerator, &FolderEnumerator::enumerated, this, &FileModel::onEnumerated);
    _enumThread.start();

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
        _itemsByPath.insert(info.filePath(), drive);
        // Loading placeholder so the expand arrow appears before the drive
        // is opened. appendFileItems will replace it on first expansion.
        FileItem* loading = new FileItem(QFileInfo(), FileType::Loading, folderPixmap, drive);
        drive->appendChild(loading);
    }
    endInsertRows();
}

FileModel::~FileModel() {
    _enumThread.quit();
    _enumThread.wait();
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
    if (role == IndexSourcePathRole) {
        if (item->fileType() != FileType::Folder) return QVariant();
        return _folderIndexes.value(item->fileInfo().filePath());
    }
    if (role == IndexImageRole) {
        if (item->fileType() != FileType::Folder) return QVariant();
        const QString src = _folderIndexes.value(item->fileInfo().filePath());
        if (src.isEmpty()) return QVariant();
        const auto it = _thumbnails.constFind(src);
        if (it == _thumbnails.constEnd()) return QVariant();
        return it.value();
    }
    if (role == Qt::DecorationRole) {
        if (item->fileType() == FileType::Folder) {
            // Drives (children of the synthetic root) get the hard-drive
            // icon; everything else gets the folder icon.
            QIcon* icon = nullptr;
            if (item->parent() == _rootItem) {
                icon = _theme->icon("hard-drive");
            }
            if (!icon) icon = _theme->icon("folder");
            if (!icon) return QVariant();
            return QVariant(*icon);
        }
    } else if (role != Qt::DisplayRole) {
        return QVariant();
    }
    // Drives (children of the synthetic root) get their drive-letter path as
    // the display name; for everything else strip down to the file name.
    if (item->parent() == _rootItem) {
        return item->fileInfo().filePath().split("/").first();
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
        // Already populated; the lazy-load runs once per session. Use
        // refreshFolder() to force a re-read.
        return;
    }

    populateFolder(dirPath, parent);
}

void FileModel::refreshFolder(FileItem* parent) {
    if (!parent || parent == _rootItem) return;

    const QModelIndex parentIdx = indexFor(parent);
    if (parent->childCount() > 0) {
        beginRemoveRows(parentIdx, 0, parent->childCount() - 1);
        for (int i = parent->childCount() - 1; i >= 0; --i) {
            FileItem* child = parent->child(i);
            forgetSubtree(child);
            parent->removeChild(i);
        }
        endRemoveRows();
    }
    populateFolder(parent->fileInfo().filePath(), parent);
}

void FileModel::populateFolder(const QString& dirPath, FileItem* parent) {
    QDir dir(dirPath);
    const QFileInfoList fileList = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDot, QDir::Name);
    applyEntries(parent, fileList);
}

void FileModel::applyEntries(FileItem* parent, const QFileInfoList& fileList) {
    if (fileList.isEmpty()) return;

    const int firstRow = parent->childCount();
    const int lastRow = firstRow + fileList.size() - 1;
    beginInsertRows(createIndex(parent->row(), 0, parent), firstRow, lastRow);

    QString firstImagePath;  // for the folder index auto-pick

    for (const auto& fileInfo : fileList) {
        FileType fileType = FileType::File;

        if (fileInfo.isDir()) {
            fileType = FileType::Folder;
        } else {
            const QString ext = fileInfo.suffix().toLower();
            if (_imageExtensions.contains(ext)) {
                fileType = FileType::Image;
                if (firstImagePath.isEmpty()) {
                    firstImagePath = fileInfo.filePath();
                }
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
        FileItem* newItem = new FileItem(fileInfo, fileType, pixmap, parent);
        parent->appendChild(newItem);

        if (fileType == FileType::Folder) {
            // Loading placeholder so the expand arrow renders before the
            // folder's own contents are scanned.
            FileItem* childItem = new FileItem(QFileInfo(), FileType::Loading, pixmap, newItem);
            newItem->appendChild(childItem);
            // Track the folder by path so it can repaint when its index
            // image's thumbnail arrives. Skip ".." (the path resolves to
            // the parent and would alias).
            if (fileInfo.fileName() != "..") {
                _itemsByPath.insert(fileInfo.filePath(), newItem);
            }
        } else if (fileType == FileType::Image) {
            _itemsByPath.insert(fileInfo.filePath(), newItem);
        }
    }

    // Auto-pick the folder's index image (first image alphabetically). The
    // user's future "set as index" feature will override this entry.
    if (!firstImagePath.isEmpty() && parent != _rootItem) {
        const QString folderPath = parent->fileInfo().filePath();
        const QString previous = _folderIndexes.value(folderPath);
        if (previous != firstImagePath) {
            if (!previous.isEmpty()) {
                _indexUsers[previous].remove(folderPath);
                if (_indexUsers[previous].isEmpty()) _indexUsers.remove(previous);
            }
            _folderIndexes.insert(folderPath, firstImagePath);
            _indexUsers[firstImagePath].insert(folderPath);
        }
    }

    endInsertRows();
}

void FileModel::forgetSubtree(FileItem* item) {
    if (!item) return;
    const QString path = item->fileInfo().filePath();
    if (!path.isEmpty()) {
        _itemsByPath.remove(path);
        _thumbnails.remove(path);
        _pending.remove(path);
        // Keep _failed so the cache's negative cache stays consistent.

        // If this path was a folder with an index assignment, drop its
        // entry; if it was the index source for any folder, untrack the
        // reverse mapping too.
        const QString prevSource = _folderIndexes.value(path);
        if (!prevSource.isEmpty()) {
            _indexUsers[prevSource].remove(path);
            if (_indexUsers[prevSource].isEmpty()) _indexUsers.remove(prevSource);
            _folderIndexes.remove(path);
        }
        if (_indexUsers.contains(path)) {
            for (const QString& folder : _indexUsers.value(path)) {
                _folderIndexes.remove(folder);
            }
            _indexUsers.remove(path);
        }
    }
    for (int i = 0; i < item->childCount(); ++i) {
        forgetSubtree(item->child(i));
    }
}

void FileModel::onThumbnailReady(QString path, QImage image) {
    _pending.remove(path);
    _failed.remove(path);
    _thumbnails.insert(path, image);
    emitDataChangedFor(path);
    // Folders that adopted this path as their index need to repaint too.
    const auto users = _indexUsers.value(path);
    for (const QString& folderPath : users) {
        emitDataChangedFor(folderPath);
    }
}

void FileModel::onThumbnailMiss(QString path) {
    _pending.remove(path);
    _thumbnails.remove(path);
    _failed.insert(path);
    emitDataChangedFor(path);
    const auto users = _indexUsers.value(path);
    for (const QString& folderPath : users) {
        emitDataChangedFor(folderPath);
    }
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
    if (it == _itemsByPath.constEnd()) return;
    FileItem* item = it.value();
    const QModelIndex idx = createIndex(item->row(), 0, item);
    emit dataChanged(idx, idx,
        { Qt::DecorationRole, ThumbnailRole, ThumbnailStateRole, IndexImageRole });
}

FileItem* FileModel::rootItem() const {
    return _rootItem;
}

QString FileModel::folderIndexSource(const QString& folderPath) const {
    return _folderIndexes.value(folderPath);
}

QImage FileModel::thumbnailFor(const QString& path) const {
    return _thumbnails.value(path);
}

void FileModel::requestEnumerate(FileItem* parent) {
    if (!parent || parent == _rootItem) return;
    // Already populated (no Loading placeholder) — nothing to do.
    if (parent->childCount() != 1
            || parent->child(0)->fileType() != FileType::Loading) {
        return;
    }
    const QString dirPath = parent->fileInfo().filePath();
    if (_enumeratingPaths.contains(dirPath)) return;
    _enumeratingPaths.insert(dirPath);
    emit requestEnumerateSignal(dirPath);
}

void FileModel::onEnumerated(QString dirPath, QFileInfoList entries) {
    _enumeratingPaths.remove(dirPath);

    auto it = _itemsByPath.constFind(dirPath);
    if (it == _itemsByPath.constEnd()) return;
    FileItem* parent = it.value();

    // If the synchronous path (appendFileItems / refreshFolder) raced and
    // already populated this folder, the Loading placeholder is gone —
    // discard our async result.
    if (parent->childCount() != 1
            || parent->child(0)->fileType() != FileType::Loading) {
        return;
    }

    beginRemoveRows(createIndex(parent->row(), 0, parent), 0, 0);
    parent->removeChild(0);
    endRemoveRows();

    applyEntries(parent, entries);
    emit folderPopulated(dirPath);
}

QModelIndex FileModel::indexFor(FileItem* item) const {
    if (!item || item == _rootItem) return QModelIndex();
    return createIndex(item->row(), 0, item);
}

QModelIndex FileModel::expandPath(const QString& path) {
    if (path.isEmpty()) return QModelIndex();
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) return QModelIndex();

    // Build the path chain from root drive to the leaf folder.
    QStringList chain;
    QDir walker(info.absoluteFilePath());
    while (true) {
        chain.prepend(walker.absolutePath());
        if (!walker.cdUp()) break;
    }

    auto normalize = [](QString s) {
        if (!s.endsWith('/')) s.append('/');
        return s;
    };

    FileItem* parent = _rootItem;
    QModelIndex parentIndex;
    for (const QString& seg : chain) {
        const QString segNorm = normalize(seg);

        // Lazy-load the parent's children if not done yet (no-op on root,
        // and on already-loaded folders).
        if (parent != _rootItem) {
            appendFileItems(parent->fileInfo().filePath(), parent);
        }

        FileItem* match = nullptr;
        for (int i = 0; i < parent->childCount(); ++i) {
            FileItem* c = parent->child(i);
            if (c->fileType() != FileType::Folder) continue;
            if (c->fileInfo().fileName() == "..") continue;
            const QString childPath = normalize(c->fileInfo().filePath());
            if (childPath.compare(segNorm, Qt::CaseInsensitive) == 0) {
                match = c;
                break;
            }
        }
        if (!match) return QModelIndex();
        parent = match;
        parentIndex = createIndex(match->row(), 0, match);
    }

    // Make sure the leaf's contents are loaded so the file list will show them.
    appendFileItems(parent->fileInfo().filePath(), parent);
    return parentIndex;
}
