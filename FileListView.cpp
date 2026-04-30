#include "FileListView.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QFileInfo>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileListViewDelegate.h"
#include "FileModel.h"
#include "Theme.h"
#include "ThumbnailCache.h"

namespace {
constexpr int kPrefetchRows = 5;
constexpr int kUpdateDebounceMs = 50;
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
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerItem);

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

    const int firstRow = std::max(0, firstAny - kPrefetchRows);
    const int lastRow  = std::min(totalRows - 1, lastAny + kPrefetchRows);

    QSet<QString> wanted;
    wanted.reserve(lastRow - firstRow + 1);

    // Pass 2: subscribe / re-prioritize in row order so the generator's
    // sequence-number tie-breaker reflects top-left → bottom-right order.
    for (int row = firstRow; row <= lastRow; ++row) {
        QModelIndex proxyIdx = model()->index(row, 0, root);
        if (!proxyIdx.isValid()) continue;
        QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!item || item->fileType() != FileType::Image) continue;

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

        const QFileInfo info = item->fileInfo();
        const QString path = info.filePath();

        wanted.insert(path);
        if (_lastSubscribed.contains(path)) {
            _cache->setPriority(path, distance);
        } else {
            _cache->subscribe(path,
                              info.lastModified().toSecsSinceEpoch(),
                              info.size(),
                              distance);
        }
    }

    // Drop subscriptions for paths that left the window.
    const QSet<QString> toRemove = _lastSubscribed - wanted;
    for (const QString& path : toRemove) {
        _cache->unsubscribe(path);
    }

    _lastSubscribed = wanted;
}
