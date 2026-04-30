#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QListView>
#include <QSet>
#include <QString>
#include <QTimer>

class Config;
class Theme;
class FileFilterModel;
class ThumbnailCache;

class FileListView : public QListView
{
    Q_OBJECT
public:
    FileListView(Config* config, Theme* theme, ThumbnailCache* cache, FileFilterModel* fileFilterModel);

    void setRootIndex(const QModelIndex& index) override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void scheduleSubscriptionUpdate();
    void updateSubscriptions();
    void onCacheReady(QString path, QImage image);
    void onCacheMiss(QString path);

private:
    void onCacheJobDone(const QString& path);
    void tryExpandWindow();

    ThumbnailCache* _cache;
    FileFilterModel* _fileFilterModel;
    QTimer _updateTimer;
    QSet<QString> _lastSubscribed;
    // Paths currently waiting on cache pipeline completion (subscribed but
    // not yet ready/failed). Drives auto-expansion.
    QSet<QString> _activeJobs;
    // Extra rows beyond the default prefetch radius. Bumped each time the
    // current active batch finishes; reset on user scroll / folder change.
    int _windowExpansion = 0;
    // True when the current window already covers the whole folder, so
    // tryExpandWindow knows to stop.
    bool _windowCoversFolder = false;
};

#endif // FILELISTVIEW_H
