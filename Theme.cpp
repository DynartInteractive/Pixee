#include "Theme.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <QDebug>

Theme::Theme(Config* config) {
    _config = config;
    qDebug() << "Current theme: " << _config->theme();
    _loadStyle();
    _loadIni();
    _loadImages();
}

void Theme::apply(QWidget* widget) {
    widget->setStyleSheet(_style);
}

QString Theme::_basePath() {
    QString postfix = "/themes/" + _config->theme() + "/";
    QString themePath = _config->userFolder() + postfix;
    QFileInfo info(themePath);
    if (info.exists(themePath)) {
        return themePath;
    }
    themePath = _config->appFolder() + postfix;
    return themePath;
}

QString Theme::realPath(QString path) {
    QString realPath = path.replace(":/", _basePath());
    QFileInfo info(realPath);
    QString result = info.exists() ? realPath : path;
    return result;
}

QPixmap* Theme::pixmap(QString name) {
    return _pixmaps[name];
}

QIcon* Theme::icon(QString name) {
    return _icons[name];
}



QColor Theme::color(QString name, QColor defaultValue) {
    QColor result(defaultValue);
    if (_values.contains(name)) {
        qDebug() << _values.value(name).toString();
        result.setNamedColor(_values.value(name).toString());
        qDebug() << result;

    }
    return result;
}

void Theme::_loadStyle() {
    if (_config->theme() == "default") {
        _style = "";
        return;
    }
    QString path = realPath(":/style.qss");
    qDebug() << "Loading style: " << path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    auto ba = file.readAll();
    _style = QString::fromUtf8(ba);
    _style.replace("url(\":/", "url(\"" + _basePath());
    file.close();
}

void Theme::_loadIni() {
    if (_config->theme() == "default") {
        _values.clear();
        return;
    }
    QString path = realPath(":/style.ini");
    qDebug() << "Loading theme config: " << path;
    QSettings settings(path, QSettings::IniFormat);
    settings.sync();
    foreach (auto group, settings.childGroups()) {
        settings.beginGroup(group);
        foreach (auto key, settings.childKeys()) {
            QString fullKey = group + "." + key;
            QVariant value = settings.value(key);
            _values.insert(fullKey, value);
            qDebug() << "Key/Value added: " << fullKey << "/" << value.toString();
        }
        settings.endGroup();
    }
}

void Theme::_loadImages() {
    _pixmaps["back"] = _createPixmap(":/icons/back-big.png");
    _pixmaps["file"] = _createPixmap(":/icons/file-big.png");
    _pixmaps["folder"] = _createPixmap(":/icons/folder-big.png");
    _pixmaps["image"] = _createPixmap(":/icons/image-big.png");
    _pixmaps["image-error"] = _createPixmap(":/icons/image-error-big.png");
    _icons["folder"] = _createIcon(":/icons/folder.png");
}

QPixmap* Theme::_createPixmap(QString path) {
    int size = _config->thumbnailSize();
    QPixmap pixmap(realPath(path));
    auto result = new QPixmap(pixmap.scaled(size - 8, size - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return result;
}

QIcon* Theme::_createIcon(QString path) {
    return new QIcon(realPath(path));
}

