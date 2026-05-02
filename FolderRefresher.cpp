#include "FolderRefresher.h"

#include <QDir>

FolderRefresher::FolderRefresher(QObject* parent)
    : QObject(parent) {}

void FolderRefresher::refresh(QString dirPath, qint64 version) {
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDot, QDir::Name);
    emit refreshed(dirPath, version, entries);
}
