#ifndef CONFIG_H
#define CONFIG_H

#include <QStringList>

class Config
{
public:
    Config();
    const QStringList imageExtensions();
    const QStringList imageFileNameFilters();
    // Extensions Qt can actually WRITE (from QImageWriter), lowercased and
    // de-duplicated — the source of truth for the Save As format dropdown, so
    // we only ever offer formats we can encode. Distinct from imageExtensions()
    // (which is read-side: QImageReader supports strictly more formats).
    const QStringList writableImageFormats();
    int thumbnailSize();
    const QString thumbnailsPath();
    bool useBackslash();
    bool hasDriveList();
    int maxThreadCount();
    int taskWorkerCount();
    const QString userFolder();
    const QString appFolder();
    const QString theme();

private:
    static QString _USER_FOLDER;
    void _setUpImageExtensions();
    void _setUpUserFolder();
    QStringList _imageExtensions;
    QStringList _imageFileNameFilters;
    QStringList _writableFormats;
    int _thumbnailSize;
    QString _thumbnailsPath;
    QString _cacheFolder;
    QString _theme;

};

#endif // CONFIG_H
