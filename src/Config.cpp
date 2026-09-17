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
#ifdef __linux__
    // XDG on Linux: $XDG_DATA_HOME/Dynart/Pixee. This matters for the Flatpak
    // build, which is granted the host filesystem so it can browse anywhere —
    // a dot-folder built from homePath() would land in the user's *real* home
    // rather than the app's sandboxed data dir. AppDataLocation resolves to
    // ~/.var/app/net.dynart.Pixee/data/... inside the sandbox and to
    // ~/.local/share/... outside it, which is the right answer both times.
    // Windows keeps ~/.pixee: existing installs live there and nothing about
    // that platform is improved by moving it.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    return _legacyUserFolder();
#endif
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
