#ifndef FILELISTVIEWDELEGATE_H
#define FILELISTVIEWDELEGATE_H

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

private:
    Config* _config;
    Theme* _theme;
    FileFilterModel* _fileFilterModel;
    QBrush _backgroundBrush;
    QBrush _borderBrush;
    QBrush _selectionBrush;
    QPen _textPen;
    void _drawPixmap(QPainter *p, QPixmap* pixmap, QRect &rect, bool border) const;
};

#endif // FILELISTVIEWDELEGATE_H
