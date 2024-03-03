#include "FileItem.h"

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QDir>
#include <QList>

FileItem::FileItem(const QFileInfo& fileInfo, const FileType fileType, QPixmap* pixmap, FileItem* parent)
    : _fileInfo(fileInfo), _parentItem(parent), _fileType(fileType), _pixmap(pixmap) {}

FileItem::~FileItem() {
    qDeleteAll(_childItems);
}

void FileItem::appendChild(FileItem* child) {
    _childItems.append(child);
}

FileItem* FileItem::child(int row) {
    return _childItems.value(row);
}

int FileItem::childCount() const {
    return FileItem::_childItems.count();
}

void FileItem::removeChild(int row) {
    _childItems.removeAt(row);
}

void FileItem::clear() {
    _childItems.clear();
}

int FileItem::row() const {
    if (_parentItem) {
        return _parentItem->_childItems.indexOf(const_cast<FileItem*>(this));
    }
    return 0;
}

QFileInfo FileItem::fileInfo() const {
    return _fileInfo;
}

FileType FileItem::fileType() const {
    return _fileType;
}

FileItem* FileItem::parent() {
    return _parentItem;
}

QPixmap* FileItem::pixmap() const {
    return _pixmap;
}

