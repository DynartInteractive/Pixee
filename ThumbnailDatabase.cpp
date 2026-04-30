#include "ThumbnailDatabase.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

ThumbnailDatabase::ThumbnailDatabase(const QString& dbPath, QObject* parent)
    : QObject(parent), _dbPath(dbPath) {
    _connectionName = QStringLiteral("pixee_thumbs_%1").arg(reinterpret_cast<quintptr>(this));
}

ThumbnailDatabase::~ThumbnailDatabase() {
    if (_db.isOpen()) {
        _db.close();
    }
    QSqlDatabase::removeDatabase(_connectionName);
}

void ThumbnailDatabase::connectDatabase() {
    _db = QSqlDatabase::addDatabase("QSQLITE", _connectionName);
    _db.setDatabaseName(_dbPath);
    if (!_db.open()) {
        qWarning() << "ThumbnailDatabase: failed to open" << _dbPath << ":" << _db.lastError().text();
        return;
    }
    QSqlQuery q(_db);
    if (!q.exec("PRAGMA journal_mode=WAL")) {
        qWarning() << "ThumbnailDatabase: WAL pragma failed:" << q.lastError().text();
    }
    const QString create =
        "CREATE TABLE IF NOT EXISTS pixee_thumbnails ("
        "  path   TEXT    PRIMARY KEY,"
        "  mtime  INTEGER NOT NULL,"
        "  size   INTEGER NOT NULL,"
        "  width  INTEGER NOT NULL,"
        "  height INTEGER NOT NULL,"
        "  data   BLOB    NOT NULL"
        ")";
    if (!q.exec(create)) {
        qWarning() << "ThumbnailDatabase: create table failed:" << q.lastError().text();
    }
}

void ThumbnailDatabase::lookup(QString path, qint64 mtime, qint64 size) {
    if (!_db.isOpen()) {
        emit notFound(path);
        return;
    }
    QSqlQuery q(_db);
    q.prepare("SELECT mtime, size, data FROM pixee_thumbnails WHERE path = ?");
    q.addBindValue(path);
    if (!q.exec()) {
        qWarning() << "ThumbnailDatabase: lookup failed for" << path << ":" << q.lastError().text();
        emit notFound(path);
        return;
    }
    if (!q.next()) {
        emit notFound(path);
        return;
    }
    const qint64 cachedMtime = q.value(0).toLongLong();
    const qint64 cachedSize = q.value(1).toLongLong();
    if (cachedMtime != mtime || cachedSize != size) {
        emit notFound(path);
        return;
    }
    const QByteArray bytes = q.value(2).toByteArray();
    QImage image;
    if (!image.loadFromData(bytes, "JPEG")) {
        qWarning() << "ThumbnailDatabase: failed to decode cached thumb for" << path;
        emit notFound(path);
        return;
    }
    emit found(path, image);
}

void ThumbnailDatabase::save(QString path, qint64 mtime, qint64 size, int width, int height, QByteArray jpegBytes) {
    if (!_db.isOpen()) {
        return;
    }
    QSqlQuery q(_db);
    q.prepare(
        "INSERT OR REPLACE INTO pixee_thumbnails (path, mtime, size, width, height, data) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue(path);
    q.addBindValue(mtime);
    q.addBindValue(size);
    q.addBindValue(width);
    q.addBindValue(height);
    q.addBindValue(jpegBytes);
    if (!q.exec()) {
        qWarning() << "ThumbnailDatabase: save failed for" << path << ":" << q.lastError().text();
    }
}
