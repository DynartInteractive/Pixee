#include "ThumbnailGenerator.h"

#include <QMetaObject>
#include <QThread>

#include "ThumbnailWorker.h"

ThumbnailGenerator::ThumbnailGenerator(int targetSize, int jpegQuality, int workerCount, QObject* parent)
    : QObject(parent) {
    if (workerCount < 1) workerCount = 1;
    _abortVersion.storeRelease(0);
    _workers.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        QThread* t = new QThread();
        ThumbnailWorker* w = new ThumbnailWorker(targetSize, jpegQuality, &_abortVersion);
        w->moveToThread(t);
        connect(t, &QThread::finished, w, &QObject::deleteLater);
        connect(w, &ThumbnailWorker::generated, this, &ThumbnailGenerator::onWorkerGenerated);
        connect(w, &ThumbnailWorker::failed, this, &ThumbnailGenerator::onWorkerFailed);
        connect(w, &ThumbnailWorker::aborted, this, &ThumbnailGenerator::onWorkerAborted);
        t->start();
        // Soft scheduler hint — thumbnails are background work; viewer load
        // and user-initiated tasks should preempt them when CPU is contested.
        t->setPriority(QThread::LowPriority);
        _workers.append({ t, w, false, QString() });
    }
}

ThumbnailGenerator::~ThumbnailGenerator() {
    // Make in-flight decodes bail at their next chunk instead of reading a
    // possibly huge file off a slow share to completion before exit.
    _abortVersion.fetchAndAddRelease(1);
    for (const auto& slot : _workers) {
        slot.thread->quit();
    }
    for (const auto& slot : _workers) {
        slot.thread->wait();
        delete slot.thread;
    }
}

void ThumbnailGenerator::enqueue(QString path, qint64 mtime, qint64 size, int priority) {
    _meta.insert(path, qMakePair(mtime, size));
    _currentPriority.insert(path, priority);
    _queue.push({ priority, ++_seq, path });
    dispatch();
}

void ThumbnailGenerator::cancel(QString path) {
    _meta.remove(path);
    _currentPriority.remove(path);
    // Queued entries are skipped at dispatch time. For an in-flight decode,
    // signal the specific worker to bail at the next chunk boundary so the
    // SMB pipe doesn't stay tied up streaming a now-invisible huge JPEG.
    if (_processing.contains(path)) {
        for (auto& slot : _workers) {
            if (slot.busy && slot.currentPath == path) {
                slot.worker->requestCurrentAbort();
                break;
            }
        }
    }
}

void ThumbnailGenerator::setPaused(bool paused) {
    if (_paused == paused) return;
    _paused = paused;
    if (!_paused) dispatch();
}

void ThumbnailGenerator::abandonAll() {
    _meta.clear();
    _currentPriority.clear();
    decltype(_queue) empty;
    std::swap(_queue, empty);
    // Bump the abort version so any worker mid-task notices the mismatch
    // at its next chunk boundary and bails out via the aborted signal.
    _abortVersion.fetchAndAddRelease(1);
    // _processing entries stay until each worker finishes (or aborts).
}

void ThumbnailGenerator::onWorkerGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes) {
    emit generated(path, mtime, size, width, height, image, jpegBytes);
    _processing.remove(path);
    // A request that arrived mid-decode (typically just a priority bump from
    // scrolling) is satisfied by this result — drop it rather than decode
    // the same file twice.
    _meta.remove(path);
    _currentPriority.remove(path);
    markIdle(sender());
    dispatch();
}

void ThumbnailGenerator::onWorkerFailed(QString path) {
    emit failed(path);
    _processing.remove(path);
    // The file genuinely failed; a request queued meanwhile would only fail
    // again (and the cache has just recorded the failure).
    _meta.remove(path);
    _currentPriority.remove(path);
    markIdle(sender());
    dispatch();
}

void ThumbnailGenerator::onWorkerAborted(QString path) {
    _processing.remove(path);
    // Cancelled and then requested again while the worker was still winding
    // down: the request is still live, so it isn't "aborted" from the cache's
    // point of view — requeue it silently instead of reporting an abort that
    // would make the cache drop its bookkeeping for a pending path.
    if (!requeueIfRequested(path)) emit aborted(path);
    markIdle(sender());
    dispatch();
}

bool ThumbnailGenerator::requeueIfRequested(const QString& path) {
    // dispatch() drops queue entries whose path is still being decoded, and
    // nothing used to put them back — the request was lost and its subscriber
    // waited forever. A live _currentPriority entry means such a request
    // exists; push it again now that the worker has let go of the path.
    const auto it = _currentPriority.constFind(path);
    if (it == _currentPriority.constEnd()) return false;
    _queue.push({ it.value(), ++_seq, path });
    return true;
}

void ThumbnailGenerator::markIdle(QObject* worker) {
    for (auto& slot : _workers) {
        if (slot.worker == worker) {
            slot.busy = false;
            slot.currentPath.clear();
            return;
        }
    }
}

void ThumbnailGenerator::dispatch() {
    if (_paused) return;
    while (true) {
        WorkerSlot* idle = nullptr;
        for (auto& slot : _workers) {
            if (!slot.busy) { idle = &slot; break; }
        }
        if (!idle) return;

        QString path;
        qint64 mtime = 0;
        qint64 size = 0;
        bool found = false;
        while (!_queue.empty()) {
            const QueueItem item = _queue.top();
            _queue.pop();

            const auto pit = _currentPriority.constFind(item.path);
            if (pit == _currentPriority.constEnd()) continue;   // cancelled
            if (pit.value() != item.priority) continue;          // stale priority
            if (_processing.contains(item.path)) continue;       // already being decoded

            path = item.path;
            const auto meta = _meta.value(path);
            mtime = meta.first;
            size = meta.second;
            _meta.remove(path);
            _currentPriority.remove(path);
            _processing.insert(path);
            found = true;
            break;
        }
        if (!found) return;

        idle->busy = true;
        idle->currentPath = path;
        // Clear any pending per-task abort BEFORE the worker starts so a
        // stale flag from the previous task doesn't trip up this one. The
        // store is on the same thread as cancel() (both in the generator's
        // thread), so it can't race with a concurrent requestCurrentAbort.
        idle->worker->clearAbort();
        emit started(path);
        const int taskVersion = _abortVersion.loadAcquire();
        QMetaObject::invokeMethod(idle->worker, "process", Qt::QueuedConnection,
            Q_ARG(QString, path), Q_ARG(qint64, mtime), Q_ARG(qint64, size),
            Q_ARG(int, taskVersion));
    }
}
