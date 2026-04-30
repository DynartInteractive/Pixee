#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QAtomicInt>
#include <QImage>
#include <QObject>
#include <QString>

// Loads a full-res image off the GUI thread for the viewer. Reads in chunks
// with an atomic abort-version snapshot (same pattern as ThumbnailWorker)
// so navigation between images can interrupt the in-flight load instead of
// waiting for the previous file to finish.
class ImageLoader : public QObject
{
    Q_OBJECT
public:
    explicit ImageLoader(QAtomicInt* abortVersion, QObject* parent = nullptr);

public slots:
    void load(QString path, int taskVersion);

signals:
    void loaded(QString path, QImage image);
    void failed(QString path);
    void aborted(QString path);

private:
    bool isAborted(int taskVersion) const;
    QAtomicInt* _abortVersion;
};

#endif // IMAGELOADER_H
