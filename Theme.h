#ifndef THEME_H
#define THEME_H

#include <QWidget>
#include "Config.h"

class Theme
{
public:
    Theme(Config* config);
    void apply(QWidget* widget);
    QColor color(QString name, QColor defaultValue);
    QString realPath(QString path);
    QPixmap* pixmap(QString name);
    QIcon* icon(QString name);

private:
    QString _basePath();
    void _loadStyle();
    void _loadIni();
    void _loadImages();
    QPixmap* _createPixmap(QString path);
    QIcon* _createIcon(QString path);
    Config* _config;
    QString _style;
    QHash<QString, QPixmap*> _pixmaps;
    QHash<QString, QIcon*> _icons;
    QHash<QString, QVariant> _values;
};

#endif // THEME_
