#include "Config.h"
#include <QImageReader>
#include <QImageWriter>
#include <QDir>
#include <QDebug>
#include <QThreadPool>
#include <QCoreApplication>

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

const QString Config::userFolder() {
    auto result = QDir::homePath() + "/" + Config::_USER_FOLDER;
    return result;
}

const QString Config::appFolder() {
    return QCoreApplication::applicationDirPath();
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
    foreach (auto format, QImageReader::supportedImageFormats()) {
        _imageExtensions << QString(format);
    }
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
    QDir dir(userFolder());
    if (!dir.exists()) {
        QDir homeDir(QDir::homePath());
        homeDir.mkdir(Config::_USER_FOLDER);
    }
    _thumbnailsPath = userFolder() + "/thumbnails.s3db";
    QFile file(_thumbnailsPath);
    if (!file.exists()) {
        QFile::copy(":/database/thumbnails.s3db", _thumbnailsPath);
        file.setPermissions(QFile::ReadUser | QFile::WriteUser);
    }
}
