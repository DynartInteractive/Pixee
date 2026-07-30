#ifndef FILEFILTERMODEL_H
#define FILEFILTERMODEL_H

#include <QSortFilterProxyModel>
#include "FileType.h"

class FileItem;

class FileFilterModel : public QSortFilterProxyModel
{
public:
    // What the leaf comparison sorts on. ".." always sorts first and folders
    // always sort before files regardless of this — the key and direction
    // only reorder items within the same group.
    enum class SortKey { Name, Created, Modified };

    explicit FileFilterModel(QObject *parent = nullptr);
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    void setAcceptedFileTypes(const QList<FileType>& acceptedFileTypes);
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    FileItem* getRootItem() const;
    void setShowDotDot(const bool value);

    void setSortKey(SortKey key);
    SortKey sortKey() const { return _sortKey; }
    // Ascending applies to the leaf comparison only; ".." and folders-first
    // are never inverted.
    void setSortAscending(bool ascending);
    bool sortAscending() const { return _sortAscending; }
private:
    QList<FileType> _acceptedFileTypes;
    bool _showDotDot = false;
    SortKey _sortKey = SortKey::Name;
    bool _sortAscending = true;
};

#endif // FILEFILTERMODEL_H
