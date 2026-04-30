#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>

class Config;
class Theme;
class FileItem;
class ThumbnailCache;

class FileModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        ThumbnailRole = Qt::UserRole + 1,
        ThumbnailStateRole
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
    // Walk and lazy-load the tree segment-by-segment until the path is
    // reached. Returns the source QModelIndex of the leaf folder, or an
    // invalid index if any segment doesn't exist or isn't a folder.
    QModelIndex expandPath(const QString& path);
    QModelIndex indexFor(FileItem* item) const;
    FileItem* rootItem() const;

private slots:
    void onThumbnailReady(QString path, QImage image);
    void onThumbnailMiss(QString path);
    void onThumbnailPending(QString path);

private:
    void populateDrives();
    void emitDataChangedFor(const QString& path);

    FileItem* _rootItem;
    Theme* _theme;
    ThumbnailCache* _cache;
    QHash<QString, FileItem*> _itemsByPath;
    QHash<QString, QImage> _thumbnails;
    QSet<QString> _pending;
    QSet<QString> _failed;
    QSet<QString> _imageExtensions;
};

#endif // FILEMODEL_H
