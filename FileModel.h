#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>

class Config;
class Theme;
class FileItem;
class ThumbnailCache;
class FolderEnumerator;
class FolderRefresher;

class FileModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        ThumbnailRole = Qt::UserRole + 1,
        ThumbnailStateRole,
        IndexImageRole,         // for FileType::Folder: QImage of the folder's index source, if loaded
        IndexSourcePathRole     // for FileType::Folder: path of the index source, if assigned
    };
    enum ThumbnailState {
        StateIdle = 0,
        StatePending,
        StateReady,
        StateFailed
    };

    explicit FileModel(Config* config, Theme* theme, ThumbnailCache* cache, QObject* parent = nullptr);
    ~FileModel();

    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    void appendFileItems(const QString& dirPath, FileItem* parent);
    // Re-read parent's directory from disk: removes existing children
    // (notifying views), then repopulates. No-op on the synthetic root.
    void refreshFolder(FileItem* parent);
    // Asynchronously enumerate parent's directory off the GUI thread.
    // No-op for already-populated folders, the synthetic root, or while
    // an enumeration for the same path is already in flight. The
    // folderPopulated signal fires when the result has been applied.
    void requestEnumerate(FileItem* parent);
    // Asynchronously re-read parent's directory off the GUI thread and
    // apply the result. No-op on the synthetic root and on lazy-unloaded
    // folders (which only have a Loading placeholder — refresh has
    // nothing to compare against; first navigation triggers the initial
    // enumeration via requestEnumerate). Coalesces back-to-back requests
    // for the same path; supersedes older in-flight results via a
    // per-folder version counter. Phase 1: falls back to refreshFolder
    // once the worker returns. Phase 2+ replaces with a diff path.
    void requestRefreshFolder(FileItem* parent);
    // Walk and lazy-load the tree segment-by-segment until the path is
    // reached. Returns the source QModelIndex of the leaf folder, or an
    // invalid index if any segment doesn't exist or isn't a folder.
    QModelIndex expandPath(const QString& path);
    QModelIndex indexFor(FileItem* item) const;
    FileItem* rootItem() const;
    // Source image used to represent a folder's index thumbnail. Empty
    // if no image was found (or no index assigned).
    QString folderIndexSource(const QString& folderPath) const;
    // Latest in-memory thumbnail for an image path (empty QImage if none).
    // Used as a placeholder while the viewer's full-res load is in flight.
    QImage thumbnailFor(const QString& path) const;

signals:
    void folderPopulated(QString dirPath);
    // Emitted after a refresh request has been resolved (either applied
    // or short-circuited because nothing changed). `changed` is false
    // when the worker found the folder identical to the live snapshot.
    // The UI uses this to refresh status-bar counts only when needed.
    void folderRefreshed(QString dirPath, bool changed);
    // A path's cached thumbnail and on-disk metadata changed. Listeners
    // that hold per-path subscriptions (FileListView) drop and re-fetch
    // so the new bytes get decoded — dataChanged alone doesn't trigger
    // resubscription, only rowsInserted / layoutChanged / modelReset do.
    void thumbnailInvalidated(QString path);
    // Internal — to the worker threads.
    void requestEnumerateSignal(QString dirPath);
    void requestRefreshSignal(QString dirPath, qint64 version);

private slots:
    void onThumbnailReady(QString path, QImage image);
    void onThumbnailMiss(QString path);
    void onThumbnailPending(QString path);
    void onEnumerated(QString dirPath, QFileInfoList entries);
    void onRefreshed(QString dirPath, qint64 version, QFileInfoList entries);

private:
    // Diff between a folder's live children and a fresh disk snapshot.
    // Computed on the GUI thread from the worker's QFileInfoList. Empty
    // diff = no visible action (the headline win — refreshes that don't
    // actually change anything cost nothing). Source-model row indices.
    struct FolderRefreshDiff {
        QFileInfoList added;                                   // disk has, model doesn't
        QList<QPair<int, QString>> removed;                    // (row, path), sorted DESCENDING by row so per-row removes don't shift later indices
        QList<QPair<int, QFileInfo>> modified;                 // (row, new info) — same path, mtime/size changed
        bool isEmpty() const {
            return added.isEmpty() && removed.isEmpty() && modified.isEmpty();
        }
    };

    void populateDrives();
    void populateFolder(const QString& dirPath, FileItem* parent);
    void applyEntries(FileItem* parent, const QFileInfoList& entries);
    FolderRefreshDiff computeDiff(FileItem* parent, const QFileInfoList& entries) const;
    void applyRefreshDiff(FileItem* parent, const FolderRefreshDiff& diff);
    // Build a FileItem from a QFileInfo (type detection + pixmap pick +
    // Loading-placeholder for folders). Pure factory — caller is
    // responsible for parent->appendChild and _itemsByPath bookkeeping.
    FileItem* createItemForFileInfo(const QFileInfo& fileInfo, FileItem* parent);
    // Re-pick the auto-index image for `parent` by scanning current
    // children. Drops the assignment if no images remain. Emits
    // dataChanged on `parent` if the choice changed.
    void repickFolderIndex(FileItem* parent);
    void forgetSubtree(FileItem* item);
    void emitDataChangedFor(const QString& path);

    FileItem* _rootItem;
    Theme* _theme;
    ThumbnailCache* _cache;
    QHash<QString, FileItem*> _itemsByPath;
    QHash<QString, QImage> _thumbnails;
    QSet<QString> _pending;
    QSet<QString> _failed;
    QSet<QString> _imageExtensions;
    // folder path -> chosen source image path (auto-picked first image, or
    // future "set as index" override).
    QHash<QString, QString> _folderIndexes;
    // source path -> folder paths that use it as their index. Lets us repaint
    // the folder cells when the source's thumbnail arrives.
    QHash<QString, QSet<QString>> _indexUsers;
    // Async enumeration plumbing.
    QThread _enumThread;
    FolderEnumerator* _enumerator;
    QSet<QString> _enumeratingPaths;
    // Async refresh plumbing — separate thread so a slow refresh never
    // queues behind initial-load enumeration (the central list waits on
    // initial-load for the folder index source).
    QThread _refreshThread;
    FolderRefresher* _refresher;
    // Per-folder monotonic version. Bumped on every requestRefreshFolder
    // call; the worker echoes it back; onRefreshed drops results whose
    // echo no longer matches (folder went away or a newer refresh
    // superseded). Mirrors the abort-version pattern from ImageLoader /
    // ThumbnailWorker.
    QHash<QString, qint64> _refreshVersions;
    // In-flight set: a refresh for this path is currently with the
    // worker. New requests for the same path while in flight are added
    // to _refreshAgain (coalescing) rather than racing the worker.
    QSet<QString> _refreshPending;
    QSet<QString> _refreshAgain;
};

#endif // FILEMODEL_H
