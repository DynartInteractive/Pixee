#include "ThumbnailCache.h"

#include "Config.h"
#include "ThumbnailDatabase.h"
#include "ThumbnailGenerator.h"

ThumbnailCache::ThumbnailCache(Config* config, QObject* parent)
    : QObject(parent) {
    _db = new ThumbnailDatabase(config->thumbnailsPath());
    _db->moveToThread(&_dbThread);

    // Generator lives on this (the GUI) thread; it owns its own pool of
    // worker threads internally.
    _generator = new ThumbnailGenerator(config->thumbnailSize(), 85,
                                        config->maxThreadCount(), this);

    connect(&_dbThread, &QThread::finished, _db, &QObject::deleteLater);
    connect(this, &ThumbnailCache::requestConnect, _db, &ThumbnailDatabase::connectDatabase);
    connect(this, &ThumbnailCache::requestLookup, _db, &ThumbnailDatabase::lookup);
    connect(this, &ThumbnailCache::requestSave, _db, &ThumbnailDatabase::save);
    connect(_db, &ThumbnailDatabase::found, this, &ThumbnailCache::onFound);
    connect(_db, &ThumbnailDatabase::notFound, this, &ThumbnailCache::onNotFound);

    connect(this, &ThumbnailCache::requestEnqueueGenerate, _generator, &ThumbnailGenerator::enqueue);
    connect(this, &ThumbnailCache::requestCancelGenerate, _generator, &ThumbnailGenerator::cancel);
    connect(this, &ThumbnailCache::requestAbandonAll, _generator, &ThumbnailGenerator::abandonAll);
    connect(_generator, &ThumbnailGenerator::generated, this, &ThumbnailCache::onGenerated);
    connect(_generator, &ThumbnailGenerator::failed, this, &ThumbnailCache::onGenerationFailed);
    // Forward "decoding has started for this path" to the model. We tie the
    // pending state to actual generator work, not to subscribe-time, so the
    // queued placeholder reflects what the workers are actively chewing on.
    connect(_generator, &ThumbnailGenerator::started, this, &ThumbnailCache::thumbnailPending);

    _dbThread.start();
    emit requestConnect();
}

ThumbnailCache::~ThumbnailCache() {
    // Destroy the generator first; its dtor stops and joins all worker threads.
    delete _generator;
    _generator = nullptr;
    _dbThread.quit();
    _dbThread.wait();
}

void ThumbnailCache::subscribe(const QString& path, qint64 mtime, qint64 size, int distance) {
    int& count = _subscribers[path];
    ++count;
    _priorities[path] = distance;

    // Negative cache: a path the generator has already failed on stays failed
    // for the rest of the session. Counter still tracks subscribers so
    // unsubscribe stays balanced, but we don't kick off a new pipeline.
    if (_failures.contains(path)) {
        return;
    }

    if (count == 1) {
        // First subscriber. If not already in flight, start a DB lookup.
        // We do not emit thumbnailPending here — that fires when the
        // generator actually picks the path up for decoding (see the
        // generator's "started" signal forwarded in the constructor),
        // so the queued placeholder reflects active work, not just queueing.
        if (!_inDb.contains(path) && !_inGen.contains(path)) {
            _inDb.insert(path);
            _pendingMeta.insert(path, qMakePair(mtime, size));
            emit requestLookup(path, mtime, size);
        }
    } else if (_inGen.contains(path)) {
        // Already past DB and queued in generator — push priority update.
        emit requestEnqueueGenerate(path, mtime, size, distance);
    }
}

void ThumbnailCache::unsubscribe(const QString& path) {
    auto it = _subscribers.find(path);
    if (it == _subscribers.end()) return;
    if (--it.value() > 0) return;

    _subscribers.erase(it);
    _priorities.remove(path);

    if (_inGen.contains(path)) {
        emit requestCancelGenerate(path);
        _inGen.remove(path);
        _pendingMeta.remove(path);
    }
    // If still in DB phase, leave it; onNotFound will see no subscriber and drop.
}

void ThumbnailCache::setPriority(const QString& path, int distance) {
    if (!_subscribers.contains(path)) return;
    _priorities[path] = distance;
    if (_inGen.contains(path)) {
        const auto meta = _pendingMeta.value(path);
        emit requestEnqueueGenerate(path, meta.first, meta.second, distance);
    }
}

void ThumbnailCache::abandonAll() {
    _subscribers.clear();
    _priorities.clear();
    _pendingMeta.clear();
    _inDb.clear();
    _inGen.clear();
    emit requestAbandonAll();
    // Note: in-flight DB lookups and the in-flight generator decode still
    // complete; their results are dropped on arrival because _subscribers
    // is empty.
}

void ThumbnailCache::onFound(QString path, QImage image) {
    _inDb.remove(path);
    _pendingMeta.remove(path);
    _priorities.remove(path);
    if (_subscribers.contains(path)) {
        emit thumbnailReady(path, image);
    }
}

void ThumbnailCache::onNotFound(QString path) {
    _inDb.remove(path);
    if (!_subscribers.contains(path)) {
        _pendingMeta.remove(path);
        return;
    }
    auto metaIt = _pendingMeta.constFind(path);
    if (metaIt == _pendingMeta.constEnd()) return;

    const int priority = _priorities.value(path, 0);
    _inGen.insert(path);
    emit requestEnqueueGenerate(path, metaIt.value().first, metaIt.value().second, priority);
}

void ThumbnailCache::onGenerated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes) {
    _inGen.remove(path);
    _pendingMeta.remove(path);
    _priorities.remove(path);
    emit requestSave(path, mtime, size, width, height, jpegBytes);
    if (_subscribers.contains(path)) {
        emit thumbnailReady(path, image);
    }
}

void ThumbnailCache::onGenerationFailed(QString path) {
    _inGen.remove(path);
    _pendingMeta.remove(path);
    _priorities.remove(path);
    _failures.insert(path);  // remember the failure for the rest of the session
    if (_subscribers.contains(path)) {
        emit thumbnailMiss(path);
    }
}
