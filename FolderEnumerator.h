#ifndef FOLDERENUMERATOR_H
#define FOLDERENUMERATOR_H

#include <QFileInfoList>
#include <QObject>
#include <QString>

// Reads directory contents off the GUI thread. Owned by FileModel; lives on
// its own QThread. One job at a time — multiple requests sent via signals are
// serialised by Qt's event queue. That's fine for our needs (folder-index
// discovery), and keeps things simple.
class FolderEnumerator : public QObject
{
    Q_OBJECT
public:
    explicit FolderEnumerator(QObject* parent = nullptr);

public slots:
    void enumerate(QString dirPath);

signals:
    void enumerated(QString dirPath, QFileInfoList entries);
};

#endif // FOLDERENUMERATOR_H
