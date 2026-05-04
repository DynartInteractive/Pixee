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

    // Per-task abort. Called from the generator (different thread) when a
    // cancel arrives for the path this worker is currently decoding. The
    // generator clears the flag with clearAbort() right before each
    // dispatch, then sets it with requestCurrentAbort() to ask the worker
    // to bail at the next chunk boundary. Both are plain atomic stores —
    // safe to call from any thread without a queued connection.
    void requestCurrentAbort() { _abortRequested.storeRelease(1); }
    void clearAbort() { _abortRequested.storeRelease(0); }
    // Same check as the private isAborted, exposed for the file-scope
    // chunked-read helper in the .cpp. Worker-thread-side; safe to call.
    bool isAbortedExternal(int taskVersion) const;

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
    QAtomicInt _abortRequested = 0;
};

#endif // THUMBNAILWORKER_H
