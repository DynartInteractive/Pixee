#ifndef FOLDERREFRESHER_H
#define FOLDERREFRESHER_H

#include <QFileInfoList>
#include <QObject>
#include <QString>

// Re-reads directory contents off the GUI thread for refresh (as opposed
// to FolderEnumerator's initial-load path). Owned by FileModel; lives on
// its own QThread so it doesn't queue behind initial-load enumerations.
//
// The version number is an opaque token the caller chose at request time
// and is echoed back unchanged. FileModel uses it to drop superseded
// results — if the user navigates away from the folder, or another
// refresh for the same folder is requested mid-flight, the older
// in-flight result is discarded by version mismatch.
class FolderRefresher : public QObject
{
    Q_OBJECT
public:
    explicit FolderRefresher(QObject* parent = nullptr);

public slots:
    void refresh(QString dirPath, qint64 version);

signals:
    void refreshed(QString dirPath, qint64 version, QFileInfoList entries);
};

#endif // FOLDERREFRESHER_H
