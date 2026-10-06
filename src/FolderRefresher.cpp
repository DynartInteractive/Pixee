#include "FolderRefresher.h"

#include <QDir>

FolderRefresher::FolderRefresher(QObject* parent)
    : QObject(parent) {}

void FolderRefresher::refresh(QString dirPath, qint64 version) {
    QDir dir(dirPath);
    QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDot, QDir::Name);
#ifndef Q_OS_WIN
    // Fetch size / times off the GUI thread — see FolderEnumerator::enumerate.
    for (QFileInfo& fi : entries) fi.stat();
#endif
    emit refreshed(dirPath, version, entries);
}
