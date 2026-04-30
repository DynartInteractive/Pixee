#ifndef THUMBNAILWORKER_H
#define THUMBNAILWORKER_H

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class ThumbnailWorker : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailWorker(int targetSize, int jpegQuality, QObject* parent = nullptr);

public slots:
    void process(QString path, qint64 mtime, qint64 size);

signals:
    void generated(QString path, qint64 mtime, qint64 size, int width, int height, QImage image, QByteArray jpegBytes);
    void failed(QString path);

private:
    int _targetSize;
    int _jpegQuality;
};

#endif // THUMBNAILWORKER_H
