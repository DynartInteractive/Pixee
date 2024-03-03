#ifndef FILEITEM_H
#define FILEITEM_H

#include <QString>
#include <QList>
#include <QFileInfo>
#include <QPixmap>

#include "FileType.h"

class FileItem {
public:
    explicit FileItem(const QFileInfo& fileInfo, const FileType fileType, QPixmap* pixmap, FileItem* parent = nullptr);
    ~FileItem();

    void appendChild(FileItem* child);
    FileItem* child(int row);
    int childCount() const;
    int row() const;
    QFileInfo fileInfo() const;
    FileType fileType() const;
    FileItem* parent();
    void removeChild(int row);
    void clear();
    QPixmap* pixmap() const;

private:
    QFileInfo _fileInfo;
    FileItem* _parentItem;
    QList<FileItem*> _childItems;
    FileType _fileType;
    QPixmap* _pixmap;
};

#endif // FILEITEM_H
