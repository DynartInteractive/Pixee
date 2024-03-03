#include "FileListViewDelegate.h"

#include <QPainter>
#include "Theme.h"
#include "FileFilterModel.h"
#include "FileItem.h"

#include <QDebug>

FileListViewDelegate::FileListViewDelegate(Config* config, Theme* theme, FileFilterModel* fileFilterModel, QWidget* parent) : QStyledItemDelegate (parent) {
    _config = config;
    _theme = theme;
    _fileFilterModel = fileFilterModel;

    _backgroundBrush = theme->color("file-item.background-color", QColor(242, 242, 242));
    _textPen = theme->color("file-item.text-color", QColor(24, 24, 24));
    _borderBrush = theme->color("file-item.border-color", QColor(255, 255, 255));
    _selectionBrush = theme->color("file-item.selection-color", QColor(0, 128, 255, 48));
}


FileListViewDelegate::~FileListViewDelegate() {
}

QSize FileListViewDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    return QSize(_config->thumbnailSize() + 4, _config->thumbnailSize() + 37);
}

void FileListViewDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (!index.isValid()) {
        return;
    }
    QModelIndex fileIndex = _fileFilterModel->mapToSource(index);
    if (!fileIndex.isValid()) {
        return;
    }
    FileItem* fileItem = static_cast<FileItem*>(fileIndex.internalPointer());
    
    auto bgRect = option.rect;
    bgRect.adjust(0, 0, -4, -37);
    auto textRect = QRect(bgRect.x() + 6, bgRect.bottom(), bgRect.width() - 12, 28);
    p->setPen(Qt::NoPen);

    // draw icon
    if (fileItem->fileType() == FileType::Folder) {
        /*if (file->isDotDot()) {
            drawPixmap(p, theme->backPixmap, bgRect, false);
        } else {*/
        _drawPixmap(p, _theme->pixmap("folder"), bgRect, false);
        //}
    } else if (fileItem->fileType() == FileType::Image) {
        p->setBrush(_backgroundBrush);
        p->drawRect(bgRect);
    }
    _drawPixmap(p, fileItem->pixmap(), bgRect, fileItem->fileType() == FileType::Folder && !fileItem->pixmap()->isNull());

    // draw selection
    if (option.state & QStyle::State_Selected) {
        auto selRect = option.rect;
        selRect.adjust(0, 0, -4, -4);
        p->setBrush(_selectionBrush);
        p->drawRect(selRect);
    }

    // draw text
    QString text = index.data().toString();
    auto font = p->font();
    QFontMetrics fontMetrics(font);
    auto elidedText = fontMetrics.elidedText(text, Qt::ElideMiddle, textRect.width());
    p->setPen(_textPen);
    p->drawText(textRect, Qt::AlignCenter, elidedText);
}

void FileListViewDelegate::_drawPixmap(QPainter *p, QPixmap* pixmap, QRect &bgRect, bool border) const {
    auto rect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, pixmap->size(), bgRect);
    /*
    if (border) {
        auto borderRect = rect;
        borderRect.adjust(-2, -2, 2, 2);
        p->setBrush(_borderBrush);
        p->drawRect(borderRect);
    }
    */
    p->drawPixmap(rect.x(), rect.y(), *pixmap);
}
