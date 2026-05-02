#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QListView>
#include <QPersistentModelIndex>
#include <QSet>
#include <QString>
#include <QTimer>

class Config;
class Theme;
class FileFilterModel;
class TaskManager;
class ThumbnailCache;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

class FileListView : public QListView
{
    Q_OBJECT
public:
    FileListView(Config* config, Theme* theme, ThumbnailCache* cache, FileFilterModel* fileFilterModel);

    void setRootIndex(const QModelIndex& index) override;

    // Wires the bits the drop handler needs without reaching back into
    // MainWindow: where to enqueue tasks, and what widget to parent
    // dialogs / toasts to. Call once after construction.
    void setDropContext(TaskManager* taskManager, QWidget* dialogParent);

    // The current multi-selection's operable paths, with the synthetic
    // ".." item filtered out. imageOpsAllowed is false when the
    // selection contains a folder or non-image file (so the menu builder
    // can grey out Scale / Convert). Used by the right-click menu, the
    // Ctrl+C shortcut, and (Phase 3) the drag-out startDrag override.
    struct Selection {
        QStringList paths;
        bool imageOpsAllowed = true;
    };
    Selection selectionPaths() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    // Toggles the dropTarget dynamic Qt property and re-polishes so the
    // QSS rule for the drop-zone highlight border picks up the change.
    // Cheap; safe to call on every dragMove tick (no-op if unchanged).
    void setDropTargetActive(bool active);

    // Folder item under the cursor during a drag, if any. When valid the
    // drop targets that folder (drop INTO sub-folder); when invalid the
    // drop targets the currently-viewed folder (full-view highlight).
    QPersistentModelIndex _dropHoverIndex;

private slots:
    void scheduleSubscriptionUpdate();
    void updateSubscriptions();
    void onCacheReady(QString path, QImage image);
    void onCacheMiss(QString path);
    void onRowsAboutToBeRemoved(const QModelIndex& parent, int first, int last);
    // FileModel signals this when a refresh diff detected an in-place
    // file modification (same path, new mtime/size). dataChanged alone
    // doesn't make the cache re-fetch — only rowsInserted / layoutChanged
    // / modelReset trigger updateSubscriptions. Drop the per-path entry
    // from our local sets and re-tick the debounce so the next
    // subscription pass re-subscribes with the new mtime/size.
    void onThumbnailInvalidated(QString path);

private:
    void onCacheJobDone(const QString& path);
    void tryExpandWindow();

    ThumbnailCache* _cache;
    FileFilterModel* _fileFilterModel;
    TaskManager* _taskManager = nullptr;
    QWidget* _dialogParent = nullptr;
    QTimer _updateTimer;
    QSet<QString> _lastSubscribed;
    // Paths currently waiting on cache pipeline completion (subscribed but
    // not yet ready/failed). Drives auto-expansion.
    QSet<QString> _activeJobs;
    // Extra rows beyond the default prefetch radius. Bumped each time the
    // current active batch finishes; reset on user scroll / folder change.
    int _windowExpansion = 0;
    // True when the current window already covers the whole folder, so
    // tryExpandWindow knows to stop.
    bool _windowCoversFolder = false;
};

#endif // FILELISTVIEW_H
