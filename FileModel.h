#ifndef FILEMODEL_H
#define FILEMODEL_H

#include <QAbstractItemModel>

class Theme;
class FileItem;

class FileModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit FileModel(Theme* theme, QObject* parent = nullptr);
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

private:
    FileItem* _rootItem;
    Theme* _theme;
};

#endif // FILEMODEL_H
