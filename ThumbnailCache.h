#ifndef THUMBNAILCACHE_H
#define THUMBNAILCACHE_H

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QThread>

class Config;
class ThumbnailDatabase;
class ThumbnailGenerator;

class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(Config* config, QObject* parent = nullptr);
    ~ThumbnailCache();

    void subscribe(const QString& path, qint64 mtime, qint64 size, int distance = 0);
    void unsubscribe(const QString& path);
    void setPriority(const QString& path, int distance);
    // Drop all subscriptions and pending work. Use on folder change to clear
    // the generator queue in one shot rather than per-path. The in-flight
    // decode (if any) still finishes but its result is discarded.
    void abandonAll();

signals:
    void thumbnailReady(QString path, QImage image);
    void thumbnailMiss(QString path);

    // Internal — connected across worker threads.
    void requestConnect();
    void requestLookup(QString path, qint64 mtime, qint64 size);
    void requestSave(QString path, qint64 mtime, qint64 size, int width, int height, QByteArray jpegBytes);
    void requestEnqueueGenerate(QString path, qint64 mtime, qint64 size, int priority);
    void requestCancelGenerate(QString path);
    void requestAbandonAll();

private slots:
    void onFound(QString path, QImage image);
    void onNotFound(QString path);
    void onGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void onGenerationFailed(QString path);

private:
    QThread _dbThread;
    ThumbnailDatabase* _db;
    ThumbnailGenerator* _generator;

    // Per-path subscriber count (paths the GUI cares about).
    QHash<QString, int> _subscribers;
    // Latest desired priority per subscribed path.
    QHash<QString, int> _priorities;
    // mtime/size cached while a path is somewhere in the pipeline.
    QHash<QString, QPair<qint64, qint64>> _pendingMeta;
    // Phase tracking: in DB lookup, or already handed to generator.
    QSet<QString> _inDb;
    QSet<QString> _inGen;
};

#endif // THUMBNAILCACHE_H
