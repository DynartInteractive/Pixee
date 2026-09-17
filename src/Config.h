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
    // True when running inside a Flatpak sandbox. Two things hinge on it:
    // the sandbox holds --filesystem=host, so the user's real home is visible
    // and must be treated as somebody else's data; and host program paths are
    // not runnable from in here (see OpenWithDialog).
    static bool isSandboxed();
    int maxThreadCount();
    int taskWorkerCount();
    const QString userFolder();
    const QString appFolder();
    // Roots under which "themes/<name>/" is looked for, in priority order:
    // the user's own folder first, then the legacy dot-folder, then the two
    // install layouts (next to the binary, and the FHS share dir).
    const QStringList themeSearchPaths();
    const QString theme();

private:
    static QString _USER_FOLDER;
    void _setUpImageExtensions();
    void _setUpUserFolder();
    static const QString _legacyUserFolder();
    QStringList _imageExtensions;
    QStringList _imageFileNameFilters;
    QStringList _writableFormats;
    int _thumbnailSize;
    QString _thumbnailsPath;
    QString _cacheFolder;
    QString _theme;

};

#endif // CONFIG_H
