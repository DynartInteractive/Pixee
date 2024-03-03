#ifndef FILEFILTERMODEL_H
#define FILEFILTERMODEL_H

#include <QSortFilterProxyModel>
#include "FileType.h"

class FileItem;

class FileFilterModel : public QSortFilterProxyModel
{
public:
    explicit FileFilterModel(QObject *parent = nullptr);
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    void setAcceptedFileTypes(const QList<FileType>& acceptedFileTypes);
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    FileItem* getRootItem() const;
    void setShowDotDot(const bool value);
private:
    QList<FileType> _acceptedFileTypes;
    bool _showDotDot = false;
};

#endif // FILEFILTERMODEL_H
