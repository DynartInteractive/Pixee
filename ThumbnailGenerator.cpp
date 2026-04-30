#include "ThumbnailGenerator.h"

#include <QMetaObject>
#include <QThread>

#include "ThumbnailWorker.h"

ThumbnailGenerator::ThumbnailGenerator(int targetSize, int jpegQuality, int workerCount, QObject* parent)
    : QObject(parent) {
    if (workerCount < 1) workerCount = 1;
    _workers.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        QThread* t = new QThread();
        ThumbnailWorker* w = new ThumbnailWorker(targetSize, jpegQuality);
        w->moveToThread(t);
        connect(t, &QThread::finished, w, &QObject::deleteLater);
        connect(w, &ThumbnailWorker::generated, this, &ThumbnailGenerator::onWorkerGenerated);
        connect(w, &ThumbnailWorker::failed, this, &ThumbnailGenerator::onWorkerFailed);
        t->start();
        _workers.append({ t, w, false });
    }
}

ThumbnailGenerator::~ThumbnailGenerator() {
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
    // Stale queue entries are skipped at dispatch time. Workers currently
    // decoding `path` finish; their result is dropped downstream.
}

void ThumbnailGenerator::abandonAll() {
    _meta.clear();
    _currentPriority.clear();
    decltype(_queue) empty;
    std::swap(_queue, empty);
    // _processing entries stay until each worker finishes naturally.
}

void ThumbnailGenerator::onWorkerGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes) {
    emit generated(path, mtime, size, width, height, image, jpegBytes);
    _processing.remove(path);
    markIdle(sender());
    dispatch();
}

void ThumbnailGenerator::onWorkerFailed(QString path) {
    emit failed(path);
    _processing.remove(path);
    markIdle(sender());
    dispatch();
}

void ThumbnailGenerator::markIdle(QObject* worker) {
    for (auto& slot : _workers) {
        if (slot.worker == worker) {
            slot.busy = false;
            return;
        }
    }
}

void ThumbnailGenerator::dispatch() {
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
        QMetaObject::invokeMethod(idle->worker, "process", Qt::QueuedConnection,
            Q_ARG(QString, path), Q_ARG(qint64, mtime), Q_ARG(qint64, size));
    }
}
