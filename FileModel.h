#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QAbstractItemModel>
#include <QFileInfoList>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QThread>

class Config;
class Theme;
class FileItem;
class ThumbnailCache;
class FolderEnumerator;

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
    // Internal — to the worker thread.
    void requestEnumerateSignal(QString dirPath);

private slots:
    void onThumbnailReady(QString path, QImage image);
    void onThumbnailMiss(QString path);
    void onThumbnailPending(QString path);
    void onEnumerated(QString dirPath, QFileInfoList entries);

private:
    void populateDrives();
    void populateFolder(const QString& dirPath, FileItem* parent);
    void applyEntries(FileItem* parent, const QFileInfoList& entries);
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
};

#endif // FILEMODEL_H
