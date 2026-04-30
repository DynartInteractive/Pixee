#ifndef THUMBNAILGENERATOR_H
#define THUMBNAILGENERATOR_H

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

#include <queue>
#include <vector>

class QThread;
class ThumbnailWorker;

// Dispatcher that owns N ThumbnailWorker objects, each on its own QThread, and
// hands the highest-priority pending request to the next idle worker.
//
// All public/private slots and the dispatch routine run on the dispatcher's
// own thread (the GUI thread by default — same thread as ThumbnailCache). The
// shared priority queue + tracking maps are therefore touched only from that
// thread, so no mutex is needed. Worker results come back via queued signals,
// which Qt serializes onto the dispatcher's thread.
class ThumbnailGenerator : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailGenerator(int targetSize, int jpegQuality, int workerCount, QObject* parent = nullptr);
    ~ThumbnailGenerator();

public slots:
    // Add or re-prioritize a request. Lower `priority` runs first.
    void enqueue(QString path, qint64 mtime, qint64 size, int priority);
    void cancel(QString path);
    // Wipe queue + tracking. In-flight worker decodes still finish; their
    // results will be discarded by the cache when no subscriber remains.
    void abandonAll();

signals:
    void generated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void failed(QString path);

private slots:
    void onWorkerGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void onWorkerFailed(QString path);

private:
    struct QueueItem {
        int priority;
        quint64 seq;
        QString path;
    };
    struct ItemCompare {
        bool operator()(const QueueItem& a, const QueueItem& b) const {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.seq > b.seq;
        }
    };
    struct WorkerSlot {
        QThread* thread;
        ThumbnailWorker* worker;
        bool busy;
    };

    void dispatch();
    void markIdle(QObject* worker);

    quint64 _seq = 0;
    std::priority_queue<QueueItem, std::vector<QueueItem>, ItemCompare> _queue;
    QHash<QString, QPair<qint64, qint64>> _meta;
    QHash<QString, int> _currentPriority;
    QSet<QString> _processing;
    QList<WorkerSlot> _workers;
};

#endif // THUMBNAILGENERATOR_H
