#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAtomicInt>
#include <QMainWindow>
#include <QDockWidget>
#include <QImage>
#include <QLineEdit>
#include <QListView>
#include <QPointer>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QTreeView>

#include "FileModel.h"
#include "FileFilterModel.h"
#include "ImageMetadata.h"
#include "Pixee.h"

class FileItem;
class FileListView;
class FolderTreeView;
class ImageLoader;
class MetadataPanel;
class MetadataReader;
class QAction;
class QMenu;
class SettingsDialog;
class TaskConflictPrompter;
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
    // When a task group finishes, remember the files it added to the folder
    // currently being viewed (if the "Select added files" setting is on) so
    // they can be selected once the folder refresh inserts their rows.
    void onGroupFinished(QStringList producedPaths);
    void openSettings();
    void openBatchRename();
    // File → Save As…: convert/export the focused image to a chosen folder /
    // name / format via a ConvertFormatTask. File → Save: overwrite the
    // original in place — dormant until an editing op marks the viewer image
    // dirty (see ViewerWidget::isModified), so today it's always disabled.
    void saveImage();
    void saveImageAs();
    void showAbout();
    void dismissViewer();
    void viewerPrev();
    void viewerNext();
    void navigateUp();
    void onImageLoaded(QString path, QImage image);
    void onImageLoadFailed(QString path);
    void onImageLoadAborted(QString path);
    // MetadataReader results for the info panel (queued from its thread).
    void onMetadataReady(QString path, ImageMetadata metadata);
    void onMetadataReadFailed(QString path);
    void toggleFullscreen();
    void showViewerContextMenu(const QPoint& pos);
    void populateViewerZoomMenu(QMenu* zoomMenu);
    void showFileListContextMenu(const QPoint& pos);
    void copyFileListSelectionToClipboard();
    void copyViewedImageToClipboard();
    void cutFileListSelectionToClipboard();
    void cutViewedImageToClipboard();
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
    void requestMetadataRead(QString path, int taskVersion);

private:
    void navigateTo(FileItem* item);
    void expandFolderTreeTo(FileItem* item);
    // Async startup path-restore. expandPath used to walk the saved chain
    // synchronously, blocking the GUI thread per directory read — painful
    // on SMB shares. beginPathRestore kicks off a chain descent driven by
    // FileModel::folderPopulated: each level requests async enumeration,
    // advances when the result lands, and finally navigateTo's the leaf.
    // Any manual navigation via navigateTo() cancels the restore.
    void beginPathRestore(const QString& targetPath);
    void advancePathRestore();
    void cancelPathRestore();
    void createMenus();
    // Re-evaluate the enabled state of the File → Save / Save As actions from
    // the current context: Save As needs a single focused image (the viewer's
    // image, or exactly one image selected in the list); Save additionally
    // needs the viewer up and its image marked modified. Called whenever that
    // context can change (viewer show/dismiss, image loaded, list selection).
    void updateSaveActions();
    void updateStatusBar(FileItem* folder);
    // Shows "Width: w | Height: h" in the status bar while the viewer is
    // active. Pass an invalid QSize() to clear (e.g. while the full-res
    // image is still loading and we have nothing accurate to report).
    void updateViewerStatusBar(const QSize& size);
    void activateImage(FileItem* item);
    void buildViewerImageList(const QString& currentPath,
                              const QSet<QString>& restrictTo = {});
    void showViewerImageAt(int index);
    // Set the file-list's current item + selection to `path`, mapping
    // through FileModel → FileFilterModel. Optionally scroll the path into
    // view. Used to keep the list selection in sync with the viewer's
    // active image when the viewer was opened with a single selection.
    void syncFileListSelectionTo(const QString& path, bool scroll);
    // Select + scroll to whichever _pendingSelectPaths currently resolve to
    // rows in the file list; leave any not-yet-present paths pending for the
    // next folder refresh. Abandons the pending set if the user has navigated
    // away from _pendingSelectFolder.
    void trySelectPending();
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
    // Metadata panel plumbing. requestMetadataFor supersedes any in-flight
    // read and kicks off a fresh one (no-op when the dock is hidden, to save
    // disk I/O on shares). currentContextImagePath returns the image the panel
    // should describe: the viewed image when the viewer is up, otherwise the
    // file list's current image (empty for none). scheduleMetadataForSelection
    // is the debounced entry point used while browsing so arrow-key scrubbing
    // doesn't fire a read per row.
    void requestMetadataFor(const QString& path);
    QString currentContextImagePath() const;
    void scheduleMetadataForSelection();
    QString displayPath(const QString& storedPath) const;
    FileItem* currentFolder() const;

    Pixee* _pixee;
    FileModel* _fileModel;
    FileFilterModel* _folderFilterModel;
    FileFilterModel* _fileFilterModel;
    QDockWidget* _dockWidget;
    TaskDockWidget* _taskDockWidget;
    TaskStatusWidget* _taskStatusWidget = nullptr;
    // Surfaces task conflict questions as blocking modal dialogs (one at a
    // time, queued). Parented to this window, so no manual delete.
    TaskConflictPrompter* _conflictPrompter = nullptr;
    QAction* _tasksToggleAction = nullptr;
    QAction* _saveAction = nullptr;
    QAction* _saveAsAction = nullptr;
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
    // True when the viewer was opened with 2+ images selected — navigation
    // is restricted to those images and the file-list selection is left
    // alone. False (the default) is the single-entry case: viewer browses
    // the whole folder and the list selection follows the active image.
    bool _viewerMultiSelect = false;
    // Async full-res loader for the viewer.
    QThread _imageLoaderThread;
    ImageLoader* _imageLoader = nullptr;
    QAtomicInt _imageAbortVersion;
    // Full-res cache for the viewer (current image + preloaded neighbours).
    // Bounded by count; LRU-evicted via _viewerCacheOrder.
    QHash<QString, QImage> _viewerImageCache;
    QStringList _viewerCacheOrder;
    // Metadata info panel (right dock) + its off-thread reader. Same
    // supersede-on-navigate pattern as the image loader: _metadataAbortVersion
    // is bumped per request so a slow read of a superseded file self-aborts.
    QDockWidget* _metadataDock = nullptr;
    MetadataPanel* _metadataPanel = nullptr;
    QAction* _metadataToggleAction = nullptr;
    QThread _metadataThread;
    MetadataReader* _metadataReader = nullptr;
    QAtomicInt _metadataAbortVersion;
    // Debounce for browser-selection-driven reads; _pendingMetadataPath is the
    // path the debounce will read when it fires.
    QTimer _metadataDebounce;
    QString _pendingMetadataPath;
    // Fullscreen restore state — remembered when entering, applied on exit.
    bool _fsMenuBarVisible = true;
    bool _fsStatusBarVisible = true;
    bool _fsDockVisible = true;
    // Debounce buffer for task-driven folder refreshes. The timer restarts
    // on each new pathTouched signal, so a batch of completions coalesces
    // into a single refresh after a quiet interval.
    QSet<QString> _touchedDirs;
    QTimer _touchedDirsTimer;
    // Files a just-finished task group added to the folder being viewed,
    // waiting to be selected once the folder refresh inserts their rows.
    // _pendingSelectFolder is the (cleaned) folder they belong to; both clear
    // when applied or when the user navigates elsewhere.
    QStringList _pendingSelectPaths;
    QString _pendingSelectFolder;
    // The modeless, stays-on-top settings window. QPointer so it auto-nulls
    // when the window closes (it's WA_DeleteOnClose).
    QPointer<SettingsDialog> _settingsDialog;
    // Async path-restore state. _restoreChain holds the remaining absolute
    // paths to descend through (root → leaf); _restoreParent is the last
    // resolved FileItem in the chain. Both clear when restore finishes or
    // is cancelled by manual navigation.
    QStringList _restoreChain;
    FileItem* _restoreParent = nullptr;
    bool _restorePending = false;
    // Image path given on the command line (if any). Set during create()
    // by scanning qApp->arguments(); consumed when the parent folder is
    // populated by triggering activateImage. Cleared if path-restore is
    // cancelled (manual nav / unreachable folder) so a later visit to the
    // same folder doesn't trigger a surprise viewer pop-up.
    QString _startupImagePath;
};
#endif // MAINWINDOW_H
