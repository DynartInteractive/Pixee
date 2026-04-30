#include "FileListViewDelegate.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include "Theme.h"
#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileModel.h"

#include <QDebug>

FileListViewDelegate::FileListViewDelegate(Config* config, Theme* theme, FileFilterModel* fileFilterModel, QWidget* parent) : QStyledItemDelegate (parent) {
    _config = config;
    _theme = theme;
    _fileFilterModel = fileFilterModel;

    _backgroundBrush = theme->color("file-item.background-color", QColor(242, 242, 242));
    _textPen = theme->color("file-item.text-color", QColor(24, 24, 24));
    _borderBrush = theme->color("file-item.border-color", QColor(255, 255, 255));
    _selectionBrush = theme->color("file-item.selection-color", QColor(0, 128, 255, 48));

    // Checker pattern for transparent thumbnails. Defaults track the system
    // palette's QPalette::Base / AlternateBase so a default-themed light UI
    // gets sensible greys; the dark theme overrides via style.ini.
    const QPalette pal = parent ? parent->palette() : QApplication::palette();
    const QColor c1 = theme->color("file-item.checked-bg-color1", pal.color(QPalette::Base));
    const QColor c2 = theme->color("file-item.checked-bg-color2", pal.color(QPalette::AlternateBase));
    constexpr int kCheckerCell = 8;
    QPixmap pattern(kCheckerCell * 2, kCheckerCell * 2);
    pattern.fill(c1);
    {
        QPainter pp(&pattern);
        pp.fillRect(kCheckerCell, 0, kCheckerCell, kCheckerCell, c2);
        pp.fillRect(0, kCheckerCell, kCheckerCell, kCheckerCell, c2);
    }
    _checkerBrush = QBrush(pattern);

    _indexMargin      = theme->intValue("file-item.index-thumbnail-margin", 24);
    _indexBorderSize  = theme->intValue("file-item.index-thumbnail-border-size", 1);
    _indexOffsetY     = theme->intValue("file-item.index-thumbnail-offset-y", 0);
    _indexBorderColor = theme->color("file-item.index-thumbnail-border-color", QColor("#888"));
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

    if (fileItem->fileType() == FileType::Image) {
        // Solid cell background for image cells. The checker pattern only
        // appears under the actual thumbnail rect so transparency in the
        // image shows through, while non-square thumbs sit on a clean colour
        // border.
        p->setBrush(_backgroundBrush);
        p->drawRect(bgRect);

        const QImage thumbImage = index.data(FileModel::ThumbnailRole).value<QImage>();
        if (!thumbImage.isNull()) {
            const QPixmap thumbPixmap = QPixmap::fromImage(thumbImage);
            const QRect imgRect = QStyle::alignedRect(
                Qt::LeftToRight, Qt::AlignCenter, thumbPixmap.size(), bgRect);
            p->setBrush(_checkerBrush);
            p->drawRect(imgRect);
            p->drawPixmap(imgRect.x(), imgRect.y(), thumbPixmap);
        } else {
            // Placeholders sit directly on the solid background — no checker.
            const int state = index.data(FileModel::ThumbnailStateRole).toInt();
            QPixmap* placeholder = fileItem->pixmap();  // default "image" pixmap
            if (state == FileModel::StateFailed) {
                QPixmap* err = _theme->pixmap("image-error");
                if (err && !err->isNull()) placeholder = err;
            } else if (state == FileModel::StatePending) {
                QPixmap* q = _theme->pixmap("image-queued");
                if (q && !q->isNull()) placeholder = q;
            }
            _drawPixmap(p, placeholder, bgRect, false);
        }
    } else {
        _drawPixmap(p, fileItem->pixmap(), bgRect, fileItem->fileType() == FileType::Folder && !fileItem->pixmap()->isNull());

        // Folder cells: overlay the auto-picked index thumbnail (if loaded)
        // on top of the folder icon, with a margin from the cell edges and
        // an optional border around the thumbnail.
        if (fileItem->fileType() == FileType::Folder
                && fileItem->fileInfo().fileName() != "..") {
            const QImage indexImage = index.data(FileModel::IndexImageRole).value<QImage>();
            if (!indexImage.isNull()) {
                _drawIndexThumbnail(p, indexImage, bgRect);
            }
        }
    }

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

void FileListViewDelegate::_drawIndexThumbnail(QPainter* p, const QImage& image, const QRect& bgRect) const {
    if (image.isNull()) return;

    const int m = _indexMargin;
    const int b = _indexBorderSize;
    const int dy = _indexOffsetY;  // shift the overlay vertically; tunes against the folder graphic
    QRect outer = bgRect.adjusted(m, m + dy, -m, -m + dy);
    if (outer.isEmpty()) return;
    // The image fits inside `outer` minus the border on each side.
    QRect imageBox = outer.adjusted(b, b, -b, -b);
    if (imageBox.isEmpty()) return;

    const QPixmap pixmap = QPixmap::fromImage(image);
    const QSize fitted = pixmap.size().scaled(imageBox.size(), Qt::KeepAspectRatio);
    const QRect imageRect = QStyle::alignedRect(
        Qt::LeftToRight, Qt::AlignCenter, fitted, imageBox);

    // Border (drawn just outside the image rect).
    if (b > 0) {
        const QRect borderRect = imageRect.adjusted(-b, -b, b, b);
        p->setPen(Qt::NoPen);
        p->setBrush(_indexBorderColor);
        p->drawRect(borderRect);
    }

    // Checker pattern under the actual image so transparency in the
    // index thumbnail (e.g. PNGs with alpha) reads as background, not
    // as the folder icon bleeding through.
    p->setPen(Qt::NoPen);
    p->setBrush(_checkerBrush);
    p->drawRect(imageRect);

    // The image itself — smooth-scaled into its rect.
    p->drawPixmap(imageRect, pixmap, pixmap.rect());
}
