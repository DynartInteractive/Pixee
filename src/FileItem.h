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
    // Replaces the cached QFileInfo (e.g. when a refresh detects the file
    // was modified in place — same path, new mtime/size). Doesn't touch
    // the file type or pixmap.
    void setFileInfo(const QFileInfo& fileInfo);
    FileType fileType() const;
    FileItem* parent();
    // Removes child at `row` and deletes it (with its subtree). Bounds-
    // checked; out-of-range is a no-op.
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
