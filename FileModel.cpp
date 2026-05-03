#include "FileModel.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QFileInfo>

#include "Config.h"
#include "Theme.h"
#include "FileItem.h"
#include "FolderEnumerator.h"
#include "FolderRefresher.h"
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

    // Separate worker thread for refresh, so a slow refresh on one folder
    // doesn't queue behind an initial-load enumeration on another.
    _refresher = new FolderRefresher();
    _refresher->moveToThread(&_refreshThread);
    connect(&_refreshThread, &QThread::finished, _refresher, &QObject::deleteLater);
    connect(this, &FileModel::requestRefreshSignal, _refresher, &FolderRefresher::refresh);
    connect(_refresher, &FolderRefresher::refreshed, this, &FileModel::onRefreshed);
    _refreshThread.start();

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
    _refreshThread.quit();
    _enumThread.wait();
    _refreshThread.wait();
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

    // ItemIsDragEnabled is what tells QListView/QTreeView "a press on this
    // row can begin a drag" — without it, IconMode press+drag falls through
    // to rubber-band selection. We add it for everything operable; the
    // actual startDrag override filters the selection (drive roots, "..").
    return QAbstractItemModel::flags(index) | Qt::ItemIsDragEnabled;
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

FileItem* FileModel::createItemForFileInfo(const QFileInfo& fileInfo, FileItem* parent) {
    FileType fileType = FileType::File;
    if (fileInfo.isDir()) {
        fileType = FileType::Folder;
    } else {
        const QString ext = fileInfo.suffix().toLower();
        if (_imageExtensions.contains(ext)) {
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
    FileItem* item = new FileItem(fileInfo, fileType, pixmap, parent);
    if (fileType == FileType::Folder) {
        // Loading placeholder so the expand arrow renders before the
        // folder's own contents are scanned.
        FileItem* placeholder = new FileItem(QFileInfo(), FileType::Loading, pixmap, item);
        item->appendChild(placeholder);
    }
    return item;
}

void FileModel::applyEntries(FileItem* parent, const QFileInfoList& fileList) {
    if (fileList.isEmpty()) return;

    const int firstRow = parent->childCount();
    const int lastRow = firstRow + fileList.size() - 1;
    beginInsertRows(createIndex(parent->row(), 0, parent), firstRow, lastRow);

    QString firstImagePath;  // for the folder index auto-pick

    for (const auto& fileInfo : fileList) {
        FileItem* newItem = createItemForFileInfo(fileInfo, parent);
        parent->appendChild(newItem);
        const FileType fileType = newItem->fileType();
        if (fileType == FileType::Folder) {
            // Skip ".." (its path resolves to the parent and would alias).
            if (fileInfo.fileName() != "..") {
                _itemsByPath.insert(fileInfo.filePath(), newItem);
            }
        } else if (fileType == FileType::Image) {
            _itemsByPath.insert(fileInfo.filePath(), newItem);
            if (firstImagePath.isEmpty()) {
                firstImagePath = fileInfo.filePath();
            }
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

FileModel::FolderRefreshDiff FileModel::computeDiff(
        FileItem* parent, const QFileInfoList& entries) const {
    FolderRefreshDiff diff;
    if (!parent) return diff;

    // Live snapshot: path -> (row, FileItem*). Filter ".." (it's a
    // synthetic per-folder navigation aid; the worker's entryInfoList
    // also includes it on QDir::NoDot so we filter on both sides).
    struct Live {
        int row;
        FileItem* item;
    };
    QHash<QString, Live> live;
    live.reserve(parent->childCount());
    for (int row = 0; row < parent->childCount(); ++row) {
        FileItem* child = parent->child(row);
        if (!child) continue;
        const QFileInfo& info = child->fileInfo();
        if (info.fileName() == "..") continue;
        const QString path = info.filePath();
        if (path.isEmpty()) continue;
        live.insert(path, { row, child });
    }

    // Walk disk side. Match by path; classify as added / modified /
    // unchanged. Whatever survives in `live` after the walk is removed.
    for (const QFileInfo& info : entries) {
        if (info.fileName() == "..") continue;
        const QString path = info.filePath();
        const auto it = live.constFind(path);
        if (it == live.constEnd()) {
            diff.added.append(info);
            continue;
        }
        const Live& l = it.value();
        // Folders: skip the modified check. A folder's mtime changes
        // whenever a child is added/removed inside it, but the folder
        // row itself doesn't display anything that depends on mtime;
        // its index thumbnail is handled via remove/add of children.
        if (l.item->fileType() != FileType::Folder) {
            const QFileInfo& oldInfo = l.item->fileInfo();
            if (oldInfo.lastModified() != info.lastModified()
                    || oldInfo.size() != info.size()) {
                diff.modified.append({ l.row, info });
            }
        }
        live.remove(path);
    }

    // Removed (row, path) pairs, sorted DESCENDING by row so the per-row
    // beginRemoveRows calls in applyRefreshDiff don't invalidate later
    // indices: removing the highest row first leaves all lower rows at
    // their original indices.
    diff.removed.reserve(live.size());
    for (auto it = live.constBegin(); it != live.constEnd(); ++it) {
        diff.removed.append({ it.value().row, it.key() });
    }
    std::sort(diff.removed.begin(), diff.removed.end(),
              [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                  return a.first > b.first;
              });

    return diff;
}

void FileModel::applyRefreshDiff(FileItem* parent, const FolderRefreshDiff& diff) {
    if (!parent || diff.isEmpty()) return;
    const QModelIndex parentIdx = createIndex(parent->row(), 0, parent);

    // (1) Modifieds first — no row count change, no index invalidation.
    // Update the cached QFileInfo in place, drop the model's own
    // thumbnail caches for the path so the next subscribe re-decodes,
    // emit dataChanged for the row, then signal listeners (FileListView)
    // to drop and re-fetch their per-path subscription.
    for (const auto& m : diff.modified) {
        const int row = m.first;
        const QFileInfo& info = m.second;
        FileItem* item = parent->child(row);
        if (!item) continue;
        item->setFileInfo(info);

        const QString path = info.filePath();
        _thumbnails.remove(path);
        _failed.remove(path);

        const QModelIndex idx = createIndex(row, 0, item);
        emit dataChanged(idx, idx,
            { Qt::DisplayRole, Qt::DecorationRole,
              ThumbnailRole, ThumbnailStateRole, IndexImageRole });

        // Folders that adopted this path as their index image need to
        // repaint too (the thumbnail will be re-decoded).
        const auto users = _indexUsers.value(path);
        for (const QString& folderPath : users) emitDataChangedFor(folderPath);

        emit thumbnailInvalidated(path);
    }

    // (2) Removes second — descending row order (set by computeDiff)
    // so removing the highest row first doesn't shift the rows of any
    // later removes. Single-row beginRemoveRows pairs.
    for (const auto& r : diff.removed) {
        // Re-resolve row each time: a previous remove at a higher row
        // doesn't shift this one (descending order), but defensive
        // re-lookup is cheap and bulletproof.
        FileItem* child = parent->child(r.first);
        if (!child) continue;
        // Sanity: if the path doesn't match any more, the live snapshot
        // raced something — skip rather than remove the wrong row.
        if (child->fileInfo().filePath() != r.second) continue;
        beginRemoveRows(parentIdx, r.first, r.first);
        forgetSubtree(child);
        parent->removeChild(r.first);  // also deletes the FileItem
        endRemoveRows();
    }

    // (3) Adds last — append to the end of parent's source-order list.
    // The proxy reorders for display via its existing lessThan, so the
    // new rows appear at their alphabetical positions automatically.
    for (const QFileInfo& info : diff.added) {
        if (info.fileName() == "..") continue;  // belt-and-suspenders
        const int row = parent->childCount();
        beginInsertRows(parentIdx, row, row);
        FileItem* item = createItemForFileInfo(info, parent);
        parent->appendChild(item);
        const FileType ft = item->fileType();
        if (ft == FileType::Folder || ft == FileType::Image) {
            _itemsByPath.insert(info.filePath(), item);
        }
        endInsertRows();
    }

    // (4) Re-pick the folder's index image. Cheap (one scan); covers
    // both the case where the previous index image was removed and the
    // case where the folder gained its first image.
    repickFolderIndex(parent);
}

void FileModel::repickFolderIndex(FileItem* parent) {
    if (!parent || parent == _rootItem) return;
    const QString folderPath = parent->fileInfo().filePath();

    // Find the alphabetically-first image among current children. Plain
    // QString '<' matches QDir::Name's case-sensitive comparison closely
    // enough; the proxy's locale-aware sort is for display, while this
    // pick is logical/persistent.
    QString firstImagePath;
    for (int i = 0; i < parent->childCount(); ++i) {
        FileItem* child = parent->child(i);
        if (!child || child->fileType() != FileType::Image) continue;
        const QString p = child->fileInfo().filePath();
        if (firstImagePath.isEmpty() || p < firstImagePath) {
            firstImagePath = p;
        }
    }

    const QString previous = _folderIndexes.value(folderPath);
    if (previous == firstImagePath) return;  // no change

    if (!previous.isEmpty()) {
        _indexUsers[previous].remove(folderPath);
        if (_indexUsers[previous].isEmpty()) _indexUsers.remove(previous);
    }
    if (firstImagePath.isEmpty()) {
        _folderIndexes.remove(folderPath);
    } else {
        _folderIndexes.insert(folderPath, firstImagePath);
        _indexUsers[firstImagePath].insert(folderPath);
    }
    // Repaint the folder's row in its grandparent (folder tree) so the
    // new index thumbnail surfaces.
    emitDataChangedFor(folderPath);
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

FileItem* FileModel::itemForPath(const QString& path) const {
    return _itemsByPath.value(path);
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

void FileModel::requestRefreshFolder(FileItem* parent) {
    if (!parent || parent == _rootItem) return;
    // Lazy-unloaded folder (only a Loading placeholder) — refresh has
    // nothing to compare against. First navigation triggers initial
    // enumeration via FileListView::updateSubscriptions → requestEnumerate;
    // a refresh on top would just race with that.
    if (parent->childCount() == 1
            && parent->child(0)->fileType() == FileType::Loading) {
        return;
    }
    const QString dirPath = parent->fileInfo().filePath();
    if (dirPath.isEmpty()) return;

    if (_refreshPending.contains(dirPath)) {
        // A refresh for this folder is already with the worker — just mark
        // that we want another pass when the current one returns. Avoids
        // racing two workers against each other on the same folder.
        _refreshAgain.insert(dirPath);
        return;
    }
    const qint64 version = _refreshVersions.value(dirPath, 0) + 1;
    _refreshVersions.insert(dirPath, version);
    _refreshPending.insert(dirPath);
    emit requestRefreshSignal(dirPath, version);
}

void FileModel::onRefreshed(QString dirPath, qint64 version, QFileInfoList entries) {
    _refreshPending.remove(dirPath);

    // Drop superseded results — folder went away, or another refresh for
    // the same folder bumped the version while we were running.
    if (_refreshVersions.value(dirPath, 0) != version) {
        // Re-fire if a coalesced request landed during the in-flight run.
        if (_refreshAgain.remove(dirPath)) {
            auto it = _itemsByPath.constFind(dirPath);
            if (it != _itemsByPath.constEnd()) requestRefreshFolder(it.value());
        }
        return;
    }
    auto it = _itemsByPath.constFind(dirPath);
    if (it == _itemsByPath.constEnd()) {
        _refreshAgain.remove(dirPath);
        return;
    }
    FileItem* parent = it.value();
    // Folder might have been navigated-into and lazy-loaded between
    // request and result, leaving it in the loading-placeholder state
    // again is unlikely but not impossible — skip if so.
    if (parent->childCount() == 1
            && parent->child(0)->fileType() == FileType::Loading) {
        _refreshAgain.remove(dirPath);
        return;
    }

    const FolderRefreshDiff diff = computeDiff(parent, entries);
    const bool changed = !diff.isEmpty();

    if (changed) {
        // Surgical updates: per-row beginInsertRows / beginRemoveRows /
        // dataChanged. Unchanged FileItem* survive untouched, so the
        // central list's selection, scroll position, and thumbnail
        // subscriptions for unchanged rows all persist.
        applyRefreshDiff(parent, diff);
    }
    // No-change short-circuit: when nothing actually changed (the common
    // case for task pathTouched and especially focus-in refresh), the
    // GUI thread does literally nothing visible — no model signals, no
    // thumbnail churn, no repaint. Just emit the bookkeeping signal
    // for status-bar listeners (Phase 5 wires them).
    emit folderRefreshed(dirPath, changed);

    if (_refreshAgain.remove(dirPath)) {
        requestRefreshFolder(parent);
    }
}

bool FileModel::renameItem(FileItem* item, const QString& newName) {
    if (!item || item == _rootItem) return false;
    if (newName.isEmpty()) return false;
    if (item->fileType() == FileType::Loading) return false;

    const QFileInfo oldInfo = item->fileInfo();
    if (oldInfo.fileName() == "..") return false;
    const QString oldPath = oldInfo.filePath();
    const QString parentDir = oldInfo.absolutePath();
    const QString newPath = QDir(parentDir).filePath(newName);
    if (oldPath == newPath) return true;  // no-op, treat as success

    if (!QFile::rename(oldPath, newPath)) return false;

    const bool wasFolder = (item->fileType() == FileType::Folder);

    item->setFileInfo(QFileInfo(newPath));

    // Path-keyed caches: rekey only when the old key actually maps to
    // this item — avoids stomping if some other code path already
    // changed the mapping mid-flight (paranoid; shouldn't happen on the
    // GUI thread but cheap).
    if (_itemsByPath.value(oldPath) == item) {
        _itemsByPath.remove(oldPath);
        _itemsByPath.insert(newPath, item);
    }
    if (auto it = _thumbnails.find(oldPath); it != _thumbnails.end()) {
        const QImage img = it.value();
        _thumbnails.erase(it);
        _thumbnails.insert(newPath, img);
    }
    if (_pending.remove(oldPath)) _pending.insert(newPath);
    if (_failed.remove(oldPath))  _failed.insert(newPath);

    if (wasFolder) {
        // If the renamed folder had its own auto-pick assignment,
        // rekey that entry from old folder path to new folder path.
        if (_folderIndexes.contains(oldPath)) {
            const QString src = _folderIndexes.take(oldPath);
            _folderIndexes.insert(newPath, src);
            if (_indexUsers.contains(src)) {
                _indexUsers[src].remove(oldPath);
                _indexUsers[src].insert(newPath);
            }
        }
        // Drop the entire subtree and restore a Loading placeholder so
        // descendants are re-enumerated under the new path on next
        // expand. Stale _folderIndexes entries pointing into the old
        // subtree may persist as harmless dangling source paths
        // (IndexImageRole resolves to an empty QImage and the folder
        // shows its plain icon). Acceptable for v1.
        const QModelIndex idx = createIndex(item->row(), 0, item);
        if (item->childCount() > 0) {
            beginRemoveRows(idx, 0, item->childCount() - 1);
            for (int i = item->childCount() - 1; i >= 0; --i) {
                FileItem* child = item->child(i);
                forgetSubtree(child);
                item->removeChild(i);
            }
            endRemoveRows();
        }
        beginInsertRows(idx, 0, 0);
        FileItem* loading = new FileItem(QFileInfo(), FileType::Loading,
                                          _theme->pixmap("folder"), item);
        item->appendChild(loading);
        endInsertRows();
    } else {
        // Image / file rename: if this path was the index source for
        // any folder, rewrite those folders' assignments to point at
        // the new path so the overlay survives without a re-decode.
        if (_indexUsers.contains(oldPath)) {
            const QSet<QString> users = _indexUsers.take(oldPath);
            _indexUsers.insert(newPath, users);
            for (const QString& folder : users) {
                _folderIndexes.insert(folder, newPath);
                emitDataChangedFor(folder);
            }
        }
    }

    const QModelIndex idx = createIndex(item->row(), 0, item);
    emit dataChanged(idx, idx,
        { Qt::DisplayRole, Qt::DecorationRole,
          ThumbnailRole, ThumbnailStateRole, IndexImageRole });

    // Auto-pick may need to re-evaluate within the parent — the renamed
    // file might have moved out of the alphabetically-first slot.
    if (FileItem* parent = item->parent()) {
        repickFolderIndex(parent);
    }

    emit pathRenamed(oldPath, newPath);

    // Lazy view refresh — the surgical fixup above keeps caches
    // consistent, but dataChanged via the proxy doesn't always force
    // the file list cells to repaint immediately (cell-level repaint
    // sometimes lags behind, especially in IconMode where the proxy
    // re-sort fires layoutChanged). Kicking a folder refresh costs
    // nothing when the surgical update already matched disk (the diff
    // is empty and applyRefreshDiff is a no-op) and guarantees the
    // view picks up the new name + thumbnail otherwise.
    if (FileItem* parent = item->parent()) {
        requestRefreshFolder(parent);
    }
    return true;
}

FileItem* FileModel::createFolder(FileItem* parent, const QString& name) {
    if (!parent || parent == _rootItem) return nullptr;
    if (name.isEmpty()) return nullptr;

    const QString parentDir = parent->fileInfo().filePath();
    QDir dir(parentDir);
    if (!dir.mkdir(name)) return nullptr;

    const QString newPath = dir.filePath(name);
    const QFileInfo info(newPath);

    // Append the row at the end of the parent's child list. The proxy's
    // alphabetical sort handles the visible position.
    const int row = parent->childCount();
    const QModelIndex parentIdx = createIndex(parent->row(), 0, parent);
    beginInsertRows(parentIdx, row, row);
    FileItem* item = createItemForFileInfo(info, parent);
    parent->appendChild(item);
    _itemsByPath.insert(newPath, item);
    endInsertRows();

    // Lazy refresh — same rationale as renameItem. No-op when the
    // surgical insert above already matched disk; covers the case
    // where the cell didn't repaint or the proxy missed the insert.
    requestRefreshFolder(parent);
    return item;
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
