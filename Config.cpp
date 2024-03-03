#include "Config.h"
#include <QImageReader>
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

int Config::thumbnailSize() {
    return _thumbnailSize;
}

const QString Config::thumbnailsPath() {
    return _thumbnailsPath;
}

const QString Config::theme() {
    return _theme;
}

void Config::_setUpImageExtensions() {
    foreach (auto format, QImageReader::supportedImageFormats()) {
        _imageExtensions << QString(format);
    }
    foreach (auto extension, _imageExtensions) {
        _imageFileNameFilters.append("*." + extension);
    }
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
