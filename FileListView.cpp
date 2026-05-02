#include "FileListView.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QUrl>
#include <QWheelEvent>

#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileListViewDelegate.h"
#include "FileModel.h"
#include "FileOpsMenuBuilder.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "Toast.h"

namespace {
constexpr int kPrefetchRows = 5;
constexpr int kUpdateDebounceMs = 50;
// How many rows the window grows by each time the current active batch
// finishes. Larger = fewer iterations to cover a big folder, but each
// batch is also bigger.
constexpr int kExpansionStep = 100;
}

FileListView::FileListView(Config* config, Theme* theme, ThumbnailCache* cache, FileFilterModel* fileFilterModel)
    : _cache(cache), _fileFilterModel(fileFilterModel) {
    setModel(fileFilterModel);

    auto delegate = new FileListViewDelegate(config, theme, fileFilterModel, this);
    setItemDelegate(delegate);

    setObjectName("fileListView");
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerItem);

    // Drag-and-drop. Drop = copy by default (Windows convention); Shift
    // forces move. setDragEnabled is for outgoing drags; the QDrag object
    // and the source-side selection MIME are set up in Phase 3.
    //
    // QAbstractScrollArea quirk: the OS delivers drag events to the
    // viewport widget, not to the view itself. setAcceptDrops on the view
    // alone is NOT enough — without the viewport accepting drops, the
    // events bubble past our handlers and the cursor stays "disallowed".
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragEnabled(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);

    _updateTimer.setSingleShot(true);
    _updateTimer.setInterval(kUpdateDebounceMs);
    connect(&_updateTimer, &QTimer::timeout, this, &FileListView::updateSubscriptions);

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &FileListView::scheduleSubscriptionUpdate);
    connect(fileFilterModel, &QAbstractItemModel::rowsInserted,
            this, &FileListView::scheduleSubscriptionUpdate);
    connect(fileFilterModel, &QAbstractItemModel::layoutChanged,
            this, &FileListView::scheduleSubscriptionUpdate);
    connect(fileFilterModel, &QAbstractItemModel::modelReset,
            this, &FileListView::scheduleSubscriptionUpdate);
    // Drop subscriptions for rows about to disappear (e.g. F5 refresh) so the
    // post-refresh updateSubscriptions tick does a fresh subscribe instead
    // of just bumping priority — the cache is what re-delivers the image.
    connect(fileFilterModel, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &FileListView::onRowsAboutToBeRemoved);

    if (_cache) {
        connect(_cache, &ThumbnailCache::thumbnailReady, this, &FileListView::onCacheReady);
        connect(_cache, &ThumbnailCache::thumbnailMiss,  this, &FileListView::onCacheMiss);
    }

    // When a subfolder finishes async enumeration, its index source becomes
    // discoverable — re-run subscriptions to pick it up. Use the debounce
    // timer directly so the auto-expand state is preserved (we don't reset
    // window expansion on a model-driven re-tick).
    if (auto* fm = qobject_cast<FileModel*>(fileFilterModel->sourceModel())) {
        connect(fm, &FileModel::folderPopulated, this, [this](const QString&) {
            _updateTimer.start();
        });
        connect(fm, &FileModel::thumbnailInvalidated,
                this, &FileListView::onThumbnailInvalidated);
    }
}

void FileListView::setRootIndex(const QModelIndex& index) {
    // Wipe the previous folder's pipeline in one shot — clears the generator's
    // priority queue immediately, instead of leaving hundreds of cancelled
    // entries to be popped-and-skipped one by one. The in-flight decode (if
    // any) still runs to completion since QImageReader::read() can't be
    // aborted; its result is discarded by the cache.
    if (_cache && !_lastSubscribed.isEmpty()) {
        _cache->abandonAll();
    }
    _lastSubscribed.clear();
    _activeJobs.clear();
    _windowExpansion = 0;
    _windowCoversFolder = false;

    QListView::setRootIndex(index);

    // Skip the debounce on folder change so the new folder's items reach the
    // generator without waiting 50 ms.
    _updateTimer.stop();
    updateSubscriptions();
}

void FileListView::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    scheduleSubscriptionUpdate();
}

void FileListView::wheelEvent(QWheelEvent* event) {
    // Qt's QListView::IconMode ignores setVerticalScrollMode(ScrollPerItem)
    // for the mouse wheel — see "Qt gotchas" in CLAUDE.md. Override so each
    // 120-unit mouse notch scrolls one visual row; pass touchpad pixelDelta
    // through unchanged for smooth scrolling.
    QScrollBar* sb = verticalScrollBar();

    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        sb->setValue(sb->value() - pixelDelta.y());
        event->accept();
        return;
    }

    const int angleDeltaY = event->angleDelta().y();
    if (angleDeltaY == 0) {
        QListView::wheelEvent(event);
        return;
    }

    int rowHeight = 0;
    if (model()) {
        const QModelIndex first = model()->index(0, 0, rootIndex());
        if (first.isValid()) {
            rowHeight = visualRect(first).height();
        }
    }
    if (rowHeight <= 0) {
        QListView::wheelEvent(event);
        return;
    }

    sb->setValue(sb->value() - (angleDeltaY * rowHeight) / 120);
    event->accept();
}

void FileListView::scheduleSubscriptionUpdate() {
    // Triggered by user activity (scroll / resize / model change). Restart
    // the prefetch growth from scratch around the new viewport.
    _windowExpansion = 0;
    _windowCoversFolder = false;
    _updateTimer.start();
}

void FileListView::updateSubscriptions() {
    if (!_cache || !model()) return;

    const QModelIndex root = rootIndex();
    const int totalRows = model()->rowCount(root);
    if (totalRows == 0) {
        for (const QString& path : _lastSubscribed) _cache->unsubscribe(path);
        _lastSubscribed.clear();
        return;
    }

    const QRect viewRect = viewport()->rect();

    // Pass 1: walk rows in order, classify each by visibility. visualRect /
    // viewport.contains() is reliable here; indexAt() at the corners is not
    // (IconMode often has whitespace at the viewport edges).
    //
    //   firstFull / lastFull = items entirely inside the viewport (distance 0)
    //   firstAny  / lastAny  = items intersecting the viewport, including
    //                          partially-visible edge items (distance 1)
    int firstAny = -1;
    int lastAny = -1;
    int firstFull = -1;
    int lastFull = -1;
    for (int row = 0; row < totalRows; ++row) {
        QModelIndex proxyIdx = model()->index(row, 0, root);
        if (!proxyIdx.isValid()) continue;
        const QRect r = visualRect(proxyIdx);
        if (!r.isValid()) continue;
        if (r.bottom() < viewRect.top()) continue;     // entirely above viewport
        if (r.top() > viewRect.bottom()) break;         // past viewport bottom
        if (firstAny == -1) firstAny = row;
        lastAny = row;
        if (viewRect.contains(r)) {
            if (firstFull == -1) firstFull = row;
            lastFull = row;
        }
    }

    if (firstAny == -1) {
        for (const QString& path : _lastSubscribed) _cache->unsubscribe(path);
        _lastSubscribed.clear();
        return;
    }

    // If nothing fits fully (very short viewport / huge thumbnails), treat the
    // partial set as the priority-0 zone instead of leaving distance 0 empty.
    const int fullStart = (firstFull == -1) ? firstAny : firstFull;
    const int fullEnd   = (lastFull  == -1) ? lastAny  : lastFull;

    const int radius = kPrefetchRows + _windowExpansion;
    const int firstRow = std::max(0, firstAny - radius);
    const int lastRow  = std::min(totalRows - 1, lastAny + radius);
    _windowCoversFolder = (firstRow == 0 && lastRow == totalRows - 1);

    QSet<QString> wanted;
    wanted.reserve(lastRow - firstRow + 1);

    auto* fileModel = qobject_cast<FileModel*>(_fileFilterModel->sourceModel());

    // Pass 2: subscribe / re-prioritize in row order so the generator's
    // sequence-number tie-breaker reflects top-left → bottom-right order.
    for (int row = firstRow; row <= lastRow; ++row) {
        QModelIndex proxyIdx = model()->index(row, 0, root);
        if (!proxyIdx.isValid()) continue;
        QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!item) continue;

        // Decide which path's thumbnail this row needs. For images, it's
        // the file itself. For folders, it's the folder's auto-picked
        // index image (skipped for ".."). Other types — placeholder only.
        QString subscribePath;
        QFileInfo subscribeInfo;
        if (item->fileType() == FileType::Image) {
            subscribeInfo = item->fileInfo();
            subscribePath = subscribeInfo.filePath();
        } else if (item->fileType() == FileType::Folder
                   && item->fileInfo().fileName() != ".."
                   && fileModel) {
            // Kick off an async enumeration if the folder isn't populated yet.
            // No-op for already-populated folders or in-flight requests; the
            // folderPopulated signal will trigger another updateSubscriptions
            // when the result arrives, and the source will resolve then.
            fileModel->requestEnumerate(item);
            const QString src = fileModel->folderIndexSource(item->fileInfo().filePath());
            if (src.isEmpty()) continue;
            subscribeInfo = QFileInfo(src);
            if (!subscribeInfo.exists() || !subscribeInfo.isFile()) continue;
            subscribePath = src;
        } else {
            continue;
        }

        if (wanted.contains(subscribePath)) continue;  // same source already handled this pass

        int distance;
        if (row >= fullStart && row <= fullEnd) {
            distance = 0;                              // fully visible
        } else if (row >= firstAny && row <= lastAny) {
            distance = 1;                              // partially visible (edge)
        } else if (row < firstAny) {
            distance = 1 + (firstAny - row);           // prefetch above
        } else {
            distance = 1 + (row - lastAny);            // prefetch below
        }

        wanted.insert(subscribePath);
        if (_lastSubscribed.contains(subscribePath)) {
            _cache->setPriority(subscribePath, distance);
        } else {
            _cache->subscribe(subscribePath,
                              subscribeInfo.lastModified().toSecsSinceEpoch(),
                              subscribeInfo.size(),
                              distance);
            _activeJobs.insert(subscribePath);
        }
    }

    // Drop subscriptions for paths that left the window.
    const QSet<QString> toRemove = _lastSubscribed - wanted;
    for (const QString& path : toRemove) {
        _cache->unsubscribe(path);
        _activeJobs.remove(path);
    }

    _lastSubscribed = wanted;
}

void FileListView::onRowsAboutToBeRemoved(const QModelIndex& parent, int first, int last) {
    if (!_cache || _lastSubscribed.isEmpty()) return;
    auto* fm = qobject_cast<FileModel*>(_fileFilterModel->sourceModel());
    if (!fm) return;

    for (int row = first; row <= last; ++row) {
        const QModelIndex proxyIdx = _fileFilterModel->index(row, 0, parent);
        if (!proxyIdx.isValid()) continue;
        const QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!item) continue;

        QString path;
        if (item->fileType() == FileType::Image) {
            path = item->fileInfo().filePath();
        } else if (item->fileType() == FileType::Folder
                   && item->fileInfo().fileName() != "..") {
            path = fm->folderIndexSource(item->fileInfo().filePath());
        }

        if (!path.isEmpty() && _lastSubscribed.contains(path)) {
            _cache->unsubscribe(path);
            _lastSubscribed.remove(path);
            _activeJobs.remove(path);
        }
    }
}

void FileListView::onThumbnailInvalidated(QString path) {
    if (!_cache) return;
    // Unsubscribe to clear the cache's per-path subscriber state, then
    // drop our local bookkeeping so the next updateSubscriptions tick
    // sees this path as a fresh subscription target — and passes the
    // new mtime/size to the cache, which will miss in the DB and kick
    // the generator. Without the unsubscribe, the cache treats it as a
    // priority bump on an existing subscription and won't re-decode.
    if (_lastSubscribed.remove(path)) {
        _cache->unsubscribe(path);
    }
    _activeJobs.remove(path);
    _updateTimer.start();
}

void FileListView::onCacheReady(QString path, QImage) {
    onCacheJobDone(path);
}

void FileListView::onCacheMiss(QString path) {
    onCacheJobDone(path);
}

void FileListView::onCacheJobDone(const QString& path) {
    if (!_activeJobs.remove(path)) return;
    if (_activeJobs.isEmpty()) {
        tryExpandWindow();
    }
}

void FileListView::setDropContext(TaskManager* taskManager, QWidget* dialogParent) {
    _taskManager = taskManager;
    _dialogParent = dialogParent;
}

FileListView::Selection FileListView::selectionPaths() const {
    Selection result;
    if (!selectionModel()) return result;
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    for (const QModelIndex& proxyIdx : sel) {
        const QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!item) continue;
        const FileType t = item->fileType();
        // ".." is a Folder-typed navigation aid, not a real folder — don't
        // let it disable image ops or contribute to the operable paths.
        const bool isDotDot = (t == FileType::Folder
                               && item->fileInfo().fileName() == "..");
        if (isDotDot) continue;
        if (t == FileType::Folder || t == FileType::File) {
            // Either disables image ops (Scale / Convert have nothing to
            // do with folders or arbitrary files). Folders flow through
            // Copy / Move / Delete via recursive expansion in the builder.
            result.imageOpsAllowed = false;
        }
        if (t == FileType::Folder || t == FileType::Image || t == FileType::File) {
            result.paths.append(item->fileInfo().filePath());
        }
    }
    return result;
}

void FileListView::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);  // we decide the actions ourselves
    const Selection sel = selectionPaths();
    if (sel.paths.isEmpty()) return;

    QMimeData* mime = FileOpsMenuBuilder::buildPathsMimeData(sel.paths);
    if (!mime) return;

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const Qt::DropAction result =
        drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);

    // External Move: target() is null (drop landed in another process,
    // typically Explorer). The receiver did the copy; we delete the
    // sources to complete the move. Internal Move drops have a non-null
    // target — our drop handler ran MoveFileTasks already, so deleting
    // again here would race with the move tasks reading the source.
    if (result == Qt::MoveAction && drag->target() == nullptr && _taskManager) {
        FileOpsMenuBuilder::enqueueDeleteForExternalMove(sel.paths, _taskManager);
    }
}

namespace {
// True iff the mime data carries at least one local file URL — what we
// can actually act on. Remote URLs (http://...) and pure-text drags
// don't apply.
bool dropHasLocalFile(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) return false;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) return true;
    }
    return false;
}

// Pixee policy: drop = copy by default (Windows convention); Shift forces
// move. Read modifiers fresh each tick — using event->dropAction() as a
// secondary signal would create a feedback loop, since our own setDropAction
// from a previous tick gets remembered and would stick on Move even after
// the user releases Shift.
Qt::DropAction pickDropAction(const QDropEvent* event) {
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        return Qt::MoveAction;
    }
    return Qt::CopyAction;
}
}

void FileListView::setDropTargetActive(bool active) {
    if (property("dropTarget").toBool() == active) return;
    setProperty("dropTarget", active);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void FileListView::dragEnterEvent(QDragEnterEvent* event) {
    if (!dropHasLocalFile(event->mimeData())) {
        event->ignore();
        return;
    }
    setDropTargetActive(true);
    event->setDropAction(pickDropAction(event));
    event->accept();
}

void FileListView::dragMoveEvent(QDragMoveEvent* event) {
    if (!dropHasLocalFile(event->mimeData())) {
        event->ignore();
        return;
    }
    // Detect folder item under cursor — that becomes the drop target
    // (drop INTO that sub-folder), and the per-item highlight replaces
    // the full-view border. Otherwise drop targets the current folder
    // and the full-view border applies.
    QPersistentModelIndex newHover;
    const QModelIndex proxyIdx = indexAt(event->position().toPoint());
    if (proxyIdx.isValid()) {
        const QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (srcIdx.isValid()) {
            FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
            // ".." is a Folder-typed navigation aid; don't accept drops on it.
            if (item && item->fileType() == FileType::Folder
                    && item->fileInfo().fileName() != "..") {
                newHover = QPersistentModelIndex(proxyIdx);
            }
        }
    }
    if (newHover != _dropHoverIndex) {
        _dropHoverIndex = newHover;
        viewport()->update();
    }
    setDropTargetActive(!_dropHoverIndex.isValid());

    event->setDropAction(pickDropAction(event));
    event->accept();
}

void FileListView::dragLeaveEvent(QDragLeaveEvent* event) {
    setDropTargetActive(false);
    if (_dropHoverIndex.isValid()) {
        _dropHoverIndex = QPersistentModelIndex();
        viewport()->update();
    }
    QListView::dragLeaveEvent(event);
}

void FileListView::dropEvent(QDropEvent* event) {
    setDropTargetActive(false);
    const QPersistentModelIndex hovered = _dropHoverIndex;
    _dropHoverIndex = QPersistentModelIndex();
    viewport()->update();

    if (!dropHasLocalFile(event->mimeData()) || !_taskManager) {
        event->ignore();
        return;
    }

    // Pick the target folder. If a folder item was under the cursor at
    // drop time, drop INTO that sub-folder. Otherwise fall back to the
    // currently-viewed folder (which doesn't exist for the drive list,
    // so reject with a Toast).
    QString destPath;
    if (hovered.isValid()) {
        const QModelIndex srcIdx = _fileFilterModel->mapToSource(hovered);
        if (srcIdx.isValid()) {
            FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
            if (item && item->fileType() == FileType::Folder) {
                destPath = item->fileInfo().filePath();
            }
        }
    }
    if (destPath.isEmpty()) {
        const QModelIndex proxyRoot = rootIndex();
        if (!proxyRoot.isValid()) {
            Toast::show(_dialogParent,
                tr("Cannot drop here — pick a folder first"), Toast::Error);
            event->ignore();
            return;
        }
        const QModelIndex srcRoot = _fileFilterModel->mapToSource(proxyRoot);
        if (!srcRoot.isValid()) {
            event->ignore();
            return;
        }
        FileItem* folder = static_cast<FileItem*>(srcRoot.internalPointer());
        if (!folder) {
            event->ignore();
            return;
        }
        destPath = folder->fileInfo().filePath();
    }

    const Qt::DropAction action = pickDropAction(event);
    const bool isMove = (action == Qt::MoveAction);

    FileOpsMenuBuilder::handleDropOrPaste(
        event->mimeData(), destPath, isMove, _taskManager, _dialogParent);

    event->setDropAction(action);
    event->acceptProposedAction();
}

void FileListView::paintEvent(QPaintEvent* event) {
    QListView::paintEvent(event);
    if (!_dropHoverIndex.isValid()) return;
    QRect r = visualRect(_dropHoverIndex);
    if (!r.isValid()) return;
    QPainter p(viewport());
    QPen pen(QColor("#5a8dee"));
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // Inset by 1 so the 2px stroke stays inside the cell rect.
    p.drawRect(r.adjusted(1, 1, -2, -2));
}

void FileListView::tryExpandWindow() {
    // Already covering the whole folder — nothing more to fetch.
    if (_windowCoversFolder) return;
    if (!_cache || !model() || !rootIndex().isValid()) {
        // Drive list view (no rootIndex) — leave alone, no folder to expand.
        // Note: the very first updateSubscriptions on app start also has
        // an invalid rootIndex if the user hasn't navigated yet.
        if (rootIndex().isValid() == false) return;
    }
    _windowExpansion += kExpansionStep;
    updateSubscriptions();
}
