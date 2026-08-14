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
    // Force a fresh decode for a single path, discarding any prior result.
    // Clears the per-session negative cache (see _failures) and bypasses the
    // DB lookup — whose stored thumbnail may have been built from an
    // incomplete file (e.g. a long GIMP export still writing). The caller
    // passes the file's *current* on-disk mtime/size so the regenerated row
    // validates against later normal subscribe() calls. Result arrives via
    // the usual thumbnailReady/thumbnailMiss signals if the path is
    // subscribed. User-triggered ("Refresh thumbnail"), so it decodes at top
    // priority.
    void refreshThumbnail(const QString& path, qint64 mtime, qint64 size);
    // Repoint a cached thumbnail after a rename/move (old → new path), so a
    // file operation reuses the existing thumbnail instead of regenerating it.
    // Stats newPath for its current mtime/size and hands the rekey to the DB
    // thread; also drops any stale per-session negative-cache entry for the old
    // path. Cheap no-op when the paths match or newPath is gone.
    void moveThumbnail(const QString& oldPath, const QString& newPath);
    // Drop all subscriptions and pending work. Use on folder change to clear
    // the generator queue in one shot rather than per-path. The in-flight
    // decode (if any) still finishes but its result is discarded.
    void abandonAll();
    // Pause / resume thumbnail decoding. While paused, queued requests stay
    // queued and no new jobs are dispatched to workers; in-flight decodes
    // run to completion. Used while the viewer is active so the full-res
    // load doesn't compete with thumbnails for SMB bandwidth.
    void setPaused(bool paused);

signals:
    void thumbnailReady(QString path, QImage image);
    void thumbnailMiss(QString path);
    void thumbnailPending(QString path);

    // Internal — connected across worker threads.
    void requestConnect();
    void requestLookup(QString path, qint64 mtime, qint64 size);
    void requestSave(QString path, qint64 mtime, qint64 size, int width, int height, QByteArray jpegBytes);
    void requestRekey(QString oldPath, QString newPath, qint64 mtime, qint64 size);
    void requestEnqueueGenerate(QString path, qint64 mtime, qint64 size, int priority);
    void requestCancelGenerate(QString path);
    void requestAbandonAll();

private slots:
    void onFound(QString path, QImage image);
    void onNotFound(QString path);
    void onGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void onGenerationFailed(QString path);
    void onGenerationAborted(QString path);

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
    // Per-session negative cache: paths the generator has already failed on.
    // Skipped on subsequent subscribe() calls so we don't keep retrying a
    // corrupt or unreadable file every time the user revisits a folder.
    QSet<QString> _failures;
};

#endif // THUMBNAILCACHE_H
