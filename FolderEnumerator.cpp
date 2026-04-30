#include "FolderEnumerator.h"

#include <QDir>

FolderEnumerator::FolderEnumerator(QObject* parent)
    : QObject(parent) {}

void FolderEnumerator::enumerate(QString dirPath) {
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDot, QDir::Name);
    emit enumerated(dirPath, entries);
}
