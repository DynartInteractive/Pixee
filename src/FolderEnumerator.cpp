#include "FolderEnumerator.h"

#include <QDir>

FolderEnumerator::FolderEnumerator(QObject* parent)
    : QObject(parent) {}

void FolderEnumerator::enumerate(QString dirPath) {
    QDir dir(dirPath);
    QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDot, QDir::Name);
#ifndef Q_OS_WIN
    // On Unix the directory read yields names and types only; size and
    // times would be stat()ed lazily on first access — on the GUI thread
    // (refresh diff, date sort, thumbnail subscribe), one network round trip
    // per file on a share. Fetch them here, off-thread. (Windows' directory
    // read already carries them, so a stat there would only add round trips.)
    for (QFileInfo& fi : entries) fi.stat();
#endif
    emit enumerated(dirPath, entries);
}
