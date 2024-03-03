#ifndef CONFIG_H
#define CONFIG_H

#include <QStringList>

class Config
{
public:
    Config();
    const QStringList imageExtensions();
    const QStringList imageFileNameFilters();
    int thumbnailSize();
    const QString thumbnailsPath();
    bool useBackslash();
    int maxThreadCount();
    const QString userFolder();
    const QString appFolder();
    const QString theme();

private:
    static QString _USER_FOLDER;
    void _setUpImageExtensions();
    void _setUpUserFolder();
    QStringList _imageExtensions;
    QStringList _imageFileNameFilters;
    int _thumbnailSize;
    QString _thumbnailsPath;
    QString _cacheFolder;
    QString _theme;

};

#endif // CONFIG_H
