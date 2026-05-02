#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QListView>
#include <QSet>
#include <QString>
#include <QTimer>

class Config;
class Theme;
class FileFilterModel;
class TaskManager;
class ThumbnailCache;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class FileListView : public QListView
{
    Q_OBJECT
public:
    FileListView(Config* config, Theme* theme, ThumbnailCache* cache, FileFilterModel* fileFilterModel);

    void setRootIndex(const QModelIndex& index) override;

    // Wires the bits the drop handler needs without reaching back into
    // MainWindow: where to enqueue tasks, and what widget to parent
    // dialogs / toasts to. Call once after construction.
    void setDropContext(TaskManager* taskManager, QWidget* dialogParent);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void scheduleSubscriptionUpdate();
    void updateSubscriptions();
    void onCacheReady(QString path, QImage image);
    void onCacheMiss(QString path);
    void onRowsAboutToBeRemoved(const QModelIndex& parent, int first, int last);

private:
    void onCacheJobDone(const QString& path);
    void tryExpandWindow();

    ThumbnailCache* _cache;
    FileFilterModel* _fileFilterModel;
    TaskManager* _taskManager = nullptr;
    QWidget* _dialogParent = nullptr;
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
