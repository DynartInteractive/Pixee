#ifndef FILELISTVIEWDELEGATE_H
#define FILELISTVIEWDELEGATE_H

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QPen>

class Config;
class Theme;
class FileFilterModel;

class FileListViewDelegate : public QStyledItemDelegate
{
public:
    FileListViewDelegate(Config* config, Theme* theme, FileFilterModel* fileFilterModel, QWidget* parent);
    virtual ~FileListViewDelegate() override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // The edge of the square image area of a cell, in pixels. Defaults to
    // Config::thumbnailSize() (256, the size thumbnails are cached at);
    // smaller values draw the same cached thumbnails scaled down, so the
    // grid gets denser without any re-decode. The index-overlay geometry
    // from the theme (margin / border / offset, tuned for 256) scales with
    // it. The view owns re-layout after a change.
    int cellSize() const { return _cellSize; }
    void setCellSize(int px);

private:
    Config* _config;
    Theme* _theme;
    FileFilterModel* _fileFilterModel;
    QBrush _backgroundBrush;
    QBrush _borderBrush;
    QBrush _selectionBrush;
    QBrush _checkerBrush;
    QPen _textPen;
    int _cellSize;
    int _indexMargin;
    int _indexBorderSize;
    int _indexOffsetY;
    QColor _indexBorderColor;
    // Theme pixmaps (folder / file / placeholders) are pre-scaled for a 256
    // cell; a smaller cell needs them smaller. Scaled once per source pixmap
    // and cell size, keyed by QPixmap::cacheKey — a handful of entries.
    mutable QHash<qint64, QPixmap> _scaledIcons;
    int scaled(int v) const;  // v * _cellSize / thumbnailSize, rounded
    const QPixmap& iconFor(QPixmap* pixmap) const;
    void _drawPixmap(QPainter *p, QPixmap* pixmap, QRect &rect, bool border) const;
    void _drawIndexThumbnail(QPainter* p, const QImage& image, const QRect& bgRect) const;
};

#endif // FILELISTVIEWDELEGATE_H
