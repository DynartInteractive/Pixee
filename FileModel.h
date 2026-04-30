#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>

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

    explicit FileModel(Theme* theme, ThumbnailCache* cache, QObject* parent = nullptr);
    ~FileModel();

    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    void appendFileItems(const QString& dirPath, FileItem* parent);
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
};

#endif // FILEMODEL_H
