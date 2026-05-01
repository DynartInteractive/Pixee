#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAtomicInt>
#include <QMainWindow>
#include <QDockWidget>
#include <QImage>
#include <QLineEdit>
#include <QListView>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QTreeView>

#include "FileModel.h"
#include "FileFilterModel.h"
#include "Pixee.h"

class FileItem;
class ImageLoader;
class TaskDockWidget;
class ViewerWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Pixee* pixee, QWidget *parent = nullptr);
    ~MainWindow();
    virtual QSize sizeHint() const override;
    virtual void closeEvent(QCloseEvent* event) override;
    void create();
    void exit();
    void goToFolderByFileIndex(const QModelIndex& fileIndex);

private slots:
    void goToPathFromLineEdit();
    void refreshCurrentFolder();
    void onTaskPathTouched(QString dir);
    void onTouchedDirsRefreshDue();
    void showAbout();
    void dismissViewer();
    void viewerPrev();
    void viewerNext();
    void navigateUp();
    void onImageLoaded(QString path, QImage image);
    void onImageLoadFailed(QString path);
    void onImageLoadAborted(QString path);
    void toggleFullscreen();
    void showViewerContextMenu(const QPoint& pos);
    void pickAndCopyCurrentImage();
    void pickAndMoveCurrentImage();
    void showFileListContextMenu(const QPoint& pos);

signals:
    void requestImageLoad(QString path, int taskVersion);

private:
    void navigateTo(FileItem* item);
    void expandFolderTreeTo(FileItem* item);
    void createMenus();
    void updateStatusBar(FileItem* folder);
    void activateImage(FileItem* item);
    void buildViewerImageList(const QString& currentPath);
    void showViewerImageAt(int index);
    void enterFullscreen();
    void exitFullscreen();
    void copyCurrentImageTo(const QString& destFolder);
    void moveCurrentImageTo(const QString& destFolder);
    void preloadViewerNeighbors(int currentIndex, int taskVersion);
    void touchViewerCache(const QString& path);
    QString displayPath(const QString& storedPath) const;
    FileItem* currentFolder() const;

    Pixee* _pixee;
    FileModel* _fileModel;
    FileFilterModel* _folderFilterModel;
    FileFilterModel* _fileFilterModel;
    QDockWidget* _dockWidget;
    TaskDockWidget* _taskDockWidget;
    QListView* _fileListView;
    QTreeView* _folderTreeView;
    QLineEdit* _pathLineEdit;
    QStackedWidget* _centerStack;
    ViewerWidget* _viewerWidget;
    // Visibility of the folder-tree dock at the moment the viewer was
    // activated, so dismissing the viewer doesn't unhide a dock the user
    // had explicitly closed.
    bool _dockWasVisible = true;
    // Ordered list of image paths in the folder the viewer was opened
    // from (in the file-list view's display order), and the index of the
    // currently-shown image inside that list.
    QStringList _viewerImagePaths;
    int _viewerIndex = -1;
    // Async full-res loader for the viewer.
    QThread _imageLoaderThread;
    ImageLoader* _imageLoader = nullptr;
    QAtomicInt _imageAbortVersion;
    // Full-res cache for the viewer (current image + preloaded neighbours).
    // Bounded by count; LRU-evicted via _viewerCacheOrder.
    QHash<QString, QImage> _viewerImageCache;
    QStringList _viewerCacheOrder;
    // Fullscreen restore state — remembered when entering, applied on exit.
    bool _fsMenuBarVisible = true;
    bool _fsStatusBarVisible = true;
    bool _fsDockVisible = true;
    // Debounce buffer for task-driven folder refreshes. The timer restarts
    // on each new pathTouched signal, so a batch of completions coalesces
    // into a single refresh after a quiet interval.
    QSet<QString> _touchedDirs;
    QTimer _touchedDirsTimer;
};
#endif // MAINWINDOW_H
