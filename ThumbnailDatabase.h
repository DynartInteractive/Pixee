#ifndef THUMBNAILDATABASE_H
#define THUMBNAILDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QImage>
#include <QByteArray>

class ThumbnailDatabase : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailDatabase(const QString& dbPath, QObject* parent = nullptr);
    ~ThumbnailDatabase();

public slots:
    void connectDatabase();
    void lookup(QString path, qint64 mtime, qint64 size);
    void save(QString path, qint64 mtime, qint64 size, int width, int height, QByteArray jpegBytes);

signals:
    void found(QString path, QImage image);
    void notFound(QString path);

private:
    QString _dbPath;
    QSqlDatabase _db;
    QString _connectionName;
};

#endif // THUMBNAILDATABASE_H
