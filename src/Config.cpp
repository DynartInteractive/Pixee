#include "Config.h"
#include "ImageFormats.h"
#include <QImageReader>
#include <QImageWriter>
#include <QDir>
#include <QDebug>
#include <QThreadPool>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

QString Config::_USER_FOLDER = ".pixee";

Config::Config() {
    _thumbnailSize = 256;
    _theme = "dark";
    _setUpImageExtensions();
    _setUpUserFolder();
}

bool Config::useBackslash() {
#ifdef __linux__
    return false;
#else
    return true;
#endif
}

// Whether the platform has a meaningful list of drives to show above the
// filesystem root. Windows does (C:, D:, network shares); Linux and macOS
// have a single "/", so a drive list there is a one-item level the user
// always has to click through. Callers root the folder tree at "/" instead
// and default to the home folder on startup. (On macOS the extra volumes
// under /Volumes stay reachable by browsing, same as /media on Linux.)
bool Config::hasDriveList() {
#if defined(__linux__) || defined(__APPLE__)
    return false;
#else
    return true;
#endif
}

bool Config::isSandboxed() {
    // The file flatpak-run drops into every sandbox; the canonical check.
    static const bool sandboxed = QFileInfo::exists("/.flatpak-info");
    return sandboxed;
}

const QString Config::userFolder() {
    // One standard location on every platform — no #ifdef. Qt only makes
    // AppLocalDataLocation differ from AppDataLocation on Windows; on Linux
    // and macOS it returns the same value, so this is byte-identical to the
    // XDG path (~/.local/share/Dynart/Pixee) that Linux already used.
    //
    // **Local, not roaming, is the point on Windows**: the folder under
    // %LOCALAPPDATA%, not the one under %APPDATA%. thumbnails.s3db is a
    // cache and grows with the library — tens to hundreds of MB — and a
    // roaming profile would copy every byte of it to a domain server on
    // each login, which is exactly what LocalAppData exists to prevent.
    //
    // Using a standard location at all (rather than a dot-folder under $HOME)
    // additionally matters for the Flatpak build, which holds
    // --filesystem=host: a path built from homePath() would be written into
    // the user's *real* home instead of the sandbox's data dir. This resolves
    // to ~/.var/app/net.dynart.Pixee/data/... inside the sandbox and to the
    // XDG path outside it, which is the right answer both times.
    //
    // _setUpUserFolder migrates an existing ~/.pixee/thumbnails.s3db across on
    // first run, so a Windows upgrade keeps the thumbnails it already decoded.
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

const QString Config::_legacyUserFolder() {
    return QDir::homePath() + "/" + Config::_USER_FOLDER;
}

const QString Config::appFolder() {
    return QCoreApplication::applicationDirPath();
}

const QStringList Config::themeSearchPaths() {
    QStringList paths;
    paths << userFolder();
    paths << _legacyUserFolder();   // pre-XDG installs kept their themes here
    paths << appFolder();
    // FHS install: the binary is <prefix>/bin/Pixee and the themes tree is
    // <prefix>/share/pixee/themes. This is the layout `make install` and the
    // Flatpak manifest produce (/app/bin + /app/share/pixee).
    paths << QFileInfo(appFolder() + "/../share/pixee").absoluteFilePath();
    paths.removeDuplicates();
    return paths;
}

const QStringList Config::pluginSearchPaths() {
    // Same four roots as themeSearchPaths, in the same order, each with a
    // "plugins" subdirectory. Kept as its own function rather than a
    // parameterised one because the two are free to diverge -- themes have a
    // legacy location plugins never had, and plugins may later want a
    // per-plugin data dir that themes have no use for.
    QStringList paths;
    paths << userFolder() + "/plugins";
    paths << _legacyUserFolder() + "/plugins";
    paths << appFolder() + "/plugins";
    paths << QFileInfo(appFolder() + "/../share/pixee/plugins").absoluteFilePath();
    paths.removeDuplicates();
    return paths;
}


const QStringList Config::imageExtensions() {
    return _imageExtensions;
}

const QStringList Config::imageFileNameFilters() {
    return _imageFileNameFilters;
}

const QStringList Config::writableImageFormats() {
    return _writableFormats;
}

int Config::thumbnailSize() {
    return _thumbnailSize;
}

const QString Config::thumbnailsPath() {
    return _thumbnailsPath;
}

const QString Config::theme() {
    return _theme;
}

int Config::maxThreadCount() {
    // Two parallel decoders keep the SMB pipe usefully busy without
    // saturating it — four hammered the share hard enough that the
    // viewer's full-res load would queue behind them.
    return 2;
}

int Config::taskWorkerCount() {
    // Two is enough to keep the queue moving even when one task is blocked
    // on a conflict prompt. Bump if batch ops feel I/O-starved.
    return 2;
}

void Config::_setUpImageExtensions() {
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    foreach (auto format, formats) {
        _imageExtensions << QString(format);
    }
    // Plus the suffixes no plugin advertises but which hold a format we can
    // already decode — see ImageFormats.h. Only FileModel's extension-based
    // classification needs these; decoding sniffs the magic bytes.
    _imageExtensions << ImageFormats::aliasExtensionsFor(formats);
    foreach (auto extension, _imageExtensions) {
        _imageFileNameFilters.append("*." + extension);
    }
    qDebug() << "Supported image formats:" << _imageExtensions;

    foreach (auto format, QImageWriter::supportedImageFormats()) {
        _writableFormats << QString::fromLatin1(format).toLower();
    }
    _writableFormats.removeDuplicates();
    qDebug() << "Writable image formats:" << _writableFormats;
}

void Config::_setUpUserFolder() {
    // mkpath, not mkdir: the XDG location is nested (<data>/Dynart/Pixee) and
    // neither level is guaranteed to exist on a fresh account.
    QDir().mkpath(userFolder());

    _thumbnailsPath = userFolder() + "/thumbnails.s3db";
    QFile file(_thumbnailsPath);
    if (file.exists()) return;

    // One-time migration off the old ~/.pixee location, so an existing install
    // keeps its cache (and the thumbnails it already paid to decode) instead of
    // silently starting over.
    //
    // Rename outside a sandbox: the old location is dead and a leftover copy
    // would only confuse. **Copy inside one**: the sandbox is granted the host
    // filesystem, so ~/.pixee there is not ours to move — it belongs to a
    // natively installed Pixee that is still using it. Moving it made the
    // Flatpak silently steal the native install's cache the first time it ran.
    const QString legacyDb = _legacyUserFolder() + "/thumbnails.s3db";
    if (legacyDb != _thumbnailsPath && QFile::exists(legacyDb)) {
        const bool migrated = isSandboxed()
            ? QFile::copy(legacyDb, _thumbnailsPath)
            : (QFile::rename(legacyDb, _thumbnailsPath) ||
               QFile::copy(legacyDb, _thumbnailsPath));
        if (migrated) {
            qDebug() << "Seeded thumbnail cache from" << legacyDb;
            file.setPermissions(QFile::ReadUser | QFile::WriteUser);
            return;
        }
    }

    QFile::copy(":/database/thumbnails.s3db", _thumbnailsPath);
    file.setPermissions(QFile::ReadUser | QFile::WriteUser);
}
