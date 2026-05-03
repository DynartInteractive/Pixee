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
class FileListView;
class FolderTreeView;
class ImageLoader;
class QAction;
class QMenu;
class TaskDockWidget;
class TaskStatusWidget;
class ViewerWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Pixee* pixee, QWidget *parent = nullptr);
    ~MainWindow();
    virtual QSize sizeHint() const override;
    virtual void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
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
    void populateViewerZoomMenu(QMenu* zoomMenu);
    void showFileListContextMenu(const QPoint& pos);
    void copyFileListSelectionToClipboard();
    void copyViewedImageToClipboard();
    void pasteIntoCurrentFolder();
    void pasteIntoViewerImageFolder();
    // Show RenameDialog for `path`, validate, drive FileModel::renameItem.
    // Toast on disk failure. Used from the Rename menu action AND the F2
    // shortcuts on the file list / viewer.
    void renameItemAt(const QString& path);
    // FileModel::pathRenamed handler — keeps the viewer's path list +
    // image cache + recent-destination QSettings consistent across a
    // rename of a currently-tracked file or folder.
    void onPathRenamed(QString oldPath, QString newPath);
    // Show NewFolderDialog for `parentDir`, validate, drive
    // FileModel::createFolder. Selects + scrolls to the new folder in
    // the file list when the parent matches the currently-viewed
    // folder. Toast on disk failure.
    void createFolderIn(const QString& parentDir);

signals:
    void requestImageLoad(QString path, int taskVersion);

private:
    void navigateTo(FileItem* item);
    void expandFolderTreeTo(FileItem* item);
    void createMenus();
    void updateStatusBar(FileItem* folder);
    // Shows "Width: w | Height: h" in the status bar while the viewer is
    // active. Pass an invalid QSize() to clear (e.g. while the full-res
    // image is still loading and we have nothing accurate to report).
    void updateViewerStatusBar(const QSize& size);
    void activateImage(FileItem* item);
    void buildViewerImageList(const QString& currentPath);
    void showViewerImageAt(int index);
    void enterFullscreen();
    void exitFullscreen();
    // Drop the currently-viewed image from the viewer's local path list
    // and either advance to the next (preferred), fall back to the previous
    // when we were on the tail, or dismiss the viewer if it was the only
    // image. Called *before* the move / delete task is enqueued so the
    // user is already looking at the next image while the task runs.
    void advanceViewerAfterRemoval();
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
    TaskStatusWidget* _taskStatusWidget = nullptr;
    QAction* _tasksToggleAction = nullptr;
    FileListView* _fileListView;
    FolderTreeView* _folderTreeView;
    QLineEdit* _pathLineEdit;
    QStackedWidget* _centerStack;
    ViewerWidget* _viewerWidget;
    // Persistent user intent for the tasks dock — what the View → Tasks
    // menu represents. Sticky across program runs (saved in QSettings).
    // Status-bar clicks toggle visibility WITHOUT touching this; menu
    // toggles and the dock's X-close DO update it.
    bool _userTasksDockEnabled = false;
    // Set true around programmatic show/hide() calls (status-bar click,
    // auto-hide-on-finish) so the dock's visibilityChanged handler
    // doesn't mistake them for user-driven X-close events and clobber
    // _userTasksDockEnabled. Reset to false in the handler after the
    // first ignored callback.
    bool _suppressDockVisibilitySync = false;
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
    // Folders that were touched by a task while the user was looking at a
    // different folder. Refreshed lazily the next time the user navigates
    // back into one of them, so the model doesn't keep serving the stale
    // pre-task contents.
    QSet<QString> _staleDirs;
};
#endif // MAINWINDOW_H
