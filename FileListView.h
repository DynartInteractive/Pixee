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

private:
    ThumbnailCache* _cache;
    FileFilterModel* _fileFilterModel;
    QTimer _updateTimer;
    QSet<QString> _lastSubscribed;
};

#endif // FILELISTVIEW_H
