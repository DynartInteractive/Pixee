#ifndef THUMBNAILWORKER_H
#define THUMBNAILWORKER_H

#include <QAtomicInt>
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class ThumbnailWorker : public QObject
{
    Q_OBJECT
public:
    // `abortVersion` is owned by the dispatcher (ThumbnailGenerator); it is
    // incremented when the dispatcher wants all in-flight workers to bail.
    explicit ThumbnailWorker(int targetSize, int jpegQuality,
                             QAtomicInt* abortVersion, QObject* parent = nullptr);

public slots:
    // taskVersion is the value of *abortVersion at dispatch time. If the
    // global has moved past it before / during work, the worker aborts.
    void process(QString path, qint64 mtime, qint64 size, int taskVersion);

signals:
    void generated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void failed(QString path);
    void aborted(QString path);

private:
    bool isAborted(int taskVersion) const;

    int _targetSize;
    int _jpegQuality;
    QAtomicInt* _abortVersion;
};

#endif // THUMBNAILWORKER_H
