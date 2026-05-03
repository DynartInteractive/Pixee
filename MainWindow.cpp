#include <QAction>
#include <QActionGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QStorageInfo>
#include <QUrl>
#include <QVBoxLayout>

#include "MainWindow.h"
#include "Config.h"
#include "CopyFileTask.h"
#include "DeleteFileTask.h"
#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileModel.h"
#include "FolderTreeView.h"
#include "FileListView.h"
#include "ConvertFormatTask.h"
#include "FileOpsMenuBuilder.h"
#include "ImageLoader.h"
#include "MoveFileTask.h"
#include "NewFolderDialog.h"
#include "RenameDialog.h"
#include "ScaleImageTask.h"
#include "TaskDockWidget.h"
#include "Toast.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskStatusWidget.h"
#include "ViewerWidget.h"

#include<QDebug>

MainWindow::MainWindow(Pixee* pixee, QWidget *parent)
    : QMainWindow(parent)
{
    _pixee = pixee;
}

void MainWindow::create() {
    setWindowTitle("Pixee");

    // Models

    _fileModel = new FileModel(_pixee->config(), _pixee->theme(), _pixee->thumbnailCache());
    QObject::connect(_fileModel, &FileModel::pathRenamed,
                     this, &MainWindow::onPathRenamed);

    _folderFilterModel = new FileFilterModel();
    _folderFilterModel->setSourceModel(_fileModel);
    _folderFilterModel->setAcceptedFileTypes({ FileType::Loading, FileType::Folder });
    _folderFilterModel->setShowDotDot(false);
    _folderFilterModel->sort(0, Qt::AscendingOrder);

    _fileFilterModel = new FileFilterModel();
    _fileFilterModel->setSourceModel(_fileModel);
    _fileFilterModel->setAcceptedFileTypes({ FileType::Folder, FileType::Image, FileType::File });
    _fileFilterModel->setShowDotDot(true);
    _fileFilterModel->sort(0, Qt::AscendingOrder);

    // Widgets

    _folderTreeView = new FolderTreeView(
        _folderFilterModel
    );
    // Drop handler routes external drops onto a tree folder through the
    // same TaskManager pipeline as the file list and Ctrl+V.
    _folderTreeView->setDropContext(_pixee->taskManager(), this);

    _fileListView = new FileListView(
        _pixee->config(),
        _pixee->theme(),
        _pixee->thumbnailCache(),
        _fileFilterModel
    );
    // Drop handler routes to the same TaskManager as the menu / Ctrl+V
    // path; toasts on rejection are parented to MainWindow.
    _fileListView->setDropContext(_pixee->taskManager(), this);

    _pathLineEdit = new QLineEdit();
    _pathLineEdit->setObjectName("pathLineEdit");
    _pathLineEdit->setFocusPolicy(Qt::ClickFocus);

    _viewerWidget = new ViewerWidget();
    _viewerWidget->setObjectName("viewerWidget");
    QObject::connect(_viewerWidget, &ViewerWidget::dismissed,
                     this, &MainWindow::dismissViewer);
    QObject::connect(_viewerWidget, &ViewerWidget::prevRequested,
                     this, &MainWindow::viewerPrev);
    QObject::connect(_viewerWidget, &ViewerWidget::nextRequested,
                     this, &MainWindow::viewerNext);
    QObject::connect(_viewerWidget, &QWidget::customContextMenuRequested,
                     this, &MainWindow::showViewerContextMenu);

    // F11 toggles fullscreen window-wide (works in browser and viewer modes).
    auto* fsShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    QObject::connect(fsShortcut, &QShortcut::activated,
                     this, &MainWindow::toggleFullscreen);

    // Backspace navigates up one folder, but only when the file list has
    // focus — that way it doesn't steal Backspace from the path edit while
    // the user is typing.
    auto* upShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), _fileListView);
    upShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(upShortcut, &QShortcut::activated,
                     this, &MainWindow::navigateUp);

    // Ctrl+C — copy paths to the system clipboard. Two shortcuts, one
    // per widget context, both Qt::WidgetShortcut so the path edit
    // (which has its own native Ctrl+C for text) isn't intercepted when
    // the user is typing.
    auto* listCopyShortcut = new QShortcut(QKeySequence::Copy, _fileListView);
    listCopyShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listCopyShortcut, &QShortcut::activated,
                     this, &MainWindow::copyFileListSelectionToClipboard);

    auto* viewerCopyShortcut = new QShortcut(QKeySequence::Copy, _viewerWidget);
    viewerCopyShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerCopyShortcut, &QShortcut::activated,
                     this, &MainWindow::copyViewedImageToClipboard);

    // Ctrl+V — paste clipboard contents into the relevant folder.
    auto* listPasteShortcut = new QShortcut(QKeySequence::Paste, _fileListView);
    listPasteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listPasteShortcut, &QShortcut::activated,
                     this, &MainWindow::pasteIntoCurrentFolder);

    auto* viewerPasteShortcut = new QShortcut(QKeySequence::Paste, _viewerWidget);
    viewerPasteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerPasteShortcut, &QShortcut::activated,
                     this, &MainWindow::pasteIntoViewerImageFolder);

    // Del / Shift+Del — delete via the same code path as the right-click
    // menu (confirmation dialog, viewer's advance-after-removal). Shift
    // skips the OS trash and hard-deletes (matches Explorer convention).
    auto deleteFromList = [this](bool toTrash) {
        const auto sel = _fileListView->selectionPaths();
        if (sel.paths.isEmpty()) return;
        FileOpsMenuBuilder builder(sel.paths, _pixee->taskManager(), this);
        builder.runDelete(toTrash);
    };
    auto deleteFromViewer = [this](bool toTrash) {
        if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
        const QString src = _viewerImagePaths.at(_viewerIndex);
        FileOpsMenuBuilder builder({src}, _pixee->taskManager(), this);
        builder.setAdvanceCallback([this]() { advanceViewerAfterRemoval(); });
        builder.runDelete(toTrash);
    };

    auto* listDeleteShortcut = new QShortcut(QKeySequence::Delete, _fileListView);
    listDeleteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listDeleteShortcut, &QShortcut::activated, this,
                     [deleteFromList]() { deleteFromList(/*toTrash=*/true); });

    auto* listHardDeleteShortcut = new QShortcut(
        QKeySequence(Qt::SHIFT | Qt::Key_Delete), _fileListView);
    listHardDeleteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listHardDeleteShortcut, &QShortcut::activated, this,
                     [deleteFromList]() { deleteFromList(/*toTrash=*/false); });

    auto* viewerDeleteShortcut = new QShortcut(QKeySequence::Delete, _viewerWidget);
    viewerDeleteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerDeleteShortcut, &QShortcut::activated, this,
                     [deleteFromViewer]() { deleteFromViewer(/*toTrash=*/true); });

    auto* viewerHardDeleteShortcut = new QShortcut(
        QKeySequence(Qt::SHIFT | Qt::Key_Delete), _viewerWidget);
    viewerHardDeleteShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerHardDeleteShortcut, &QShortcut::activated, this,
                     [deleteFromViewer]() { deleteFromViewer(/*toTrash=*/false); });

    // F2 — rename. Single-selection only on the file list; the viewer
    // always operates on the current image. Both shortcuts are
    // WidgetShortcut so the path-edit (which has no rename concept) and
    // the folder tree don't intercept.
    auto* listRenameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), _fileListView);
    listRenameShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listRenameShortcut, &QShortcut::activated, this, [this]() {
        const auto sel = _fileListView->selectionPaths();
        if (sel.paths.size() != 1) return;
        renameItemAt(sel.paths.first());
    });
    auto* viewerRenameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), _viewerWidget);
    viewerRenameShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerRenameShortcut, &QShortcut::activated, this, [this]() {
        if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
        renameItemAt(_viewerImagePaths.at(_viewerIndex));
    });

    // F7 — new folder. File list creates inside the currently viewed
    // folder; viewer creates inside the parent of the current image
    // (matches Paste's destination semantics in each context).
    auto* listNewFolderShortcut = new QShortcut(QKeySequence(Qt::Key_F7), _fileListView);
    listNewFolderShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(listNewFolderShortcut, &QShortcut::activated, this, [this]() {
        FileItem* folder = currentFolder();
        if (!folder || folder == _fileModel->rootItem()) return;
        createFolderIn(folder->fileInfo().filePath());
    });
    auto* viewerNewFolderShortcut = new QShortcut(QKeySequence(Qt::Key_F7), _viewerWidget);
    viewerNewFolderShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(viewerNewFolderShortcut, &QShortcut::activated, this, [this]() {
        if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
        createFolderIn(QFileInfo(_viewerImagePaths.at(_viewerIndex)).absolutePath());
    });

    // Async image loader for the viewer. The atomic abort version is
    // bumped whenever we ask for a different image, so the previous
    // in-flight load aborts at the next chunk boundary instead of
    // blocking the new one.
    _imageAbortVersion.storeRelease(0);
    _imageLoader = new ImageLoader(&_imageAbortVersion);
    _imageLoader->moveToThread(&_imageLoaderThread);
    QObject::connect(&_imageLoaderThread, &QThread::finished,
                     _imageLoader, &QObject::deleteLater);
    QObject::connect(this, &MainWindow::requestImageLoad,
                     _imageLoader, &ImageLoader::load);
    QObject::connect(_imageLoader, &ImageLoader::loaded,
                     this, &MainWindow::onImageLoaded);
    QObject::connect(_imageLoader, &ImageLoader::failed,
                     this, &MainWindow::onImageLoadFailed);
    QObject::connect(_imageLoader, &ImageLoader::aborted,
                     this, &MainWindow::onImageLoadAborted);
    _imageLoaderThread.start();
    // Soft hint to the OS scheduler — the viewer load should beat tasks and
    // thumbnails when there's contention. This is not enforcement; v1 does
    // not gate any subsystem from any other.
    _imageLoaderThread.setPriority(QThread::HighPriority);

    // Browser "page" — path edit + file grid in a vertical layout. This is
    // the entire chrome that's visible in normal browsing. The viewer is a
    // sibling page in the same stack, so swapping pages naturally hides the
    // path edit. The folder-tree dock is separate (it's docked to the main
    // window) and is hidden / restored explicitly in activateImage / dismiss.
    auto browserPage = new QWidget();
    browserPage->setObjectName("browserPage");
    auto bv = new QVBoxLayout(browserPage);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(4);
    bv->addWidget(_pathLineEdit);
    bv->addWidget(_fileListView);

    _centerStack = new QStackedWidget();
    _centerStack->setObjectName("centerStack");
    _centerStack->addWidget(browserPage);      // index 0 — browser
    _centerStack->addWidget(_viewerWidget);    // index 1 — viewer

    // Layout

    _dockWidget = new QDockWidget("Folders");
    _dockWidget->setObjectName("foldersDockWidget");
    _dockWidget->setWidget(_folderTreeView);
    _dockWidget->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, _dockWidget);

    _taskDockWidget = new TaskDockWidget(_pixee->taskManager());
    addDockWidget(Qt::BottomDockWidgetArea, _taskDockWidget);

    // Folder auto-refresh after task completion. The timer is single-shot
    // and gets restarted on each pathTouched, so a burst of completions
    // (e.g. a 50-file move batch) coalesces into one refresh once the
    // queue goes quiet for the configured interval.
    _touchedDirsTimer.setSingleShot(true);
    _touchedDirsTimer.setInterval(500);
    connect(&_touchedDirsTimer, &QTimer::timeout,
            this, &MainWindow::onTouchedDirsRefreshDue);
    connect(_pixee->taskManager(), &TaskManager::pathTouched,
            this, &MainWindow::onTaskPathTouched);

    // Status bar reflects the post-refresh contents. Fires for every
    // resolved refresh (changed or not — we skip the FileItem walk when
    // we know the count didn't shift, but the 'changed=false' case
    // doesn't need to repaint either since the displayed counts
    // already match disk). Path-match filter so a refresh for some
    // other folder (e.g. a non-current folder that a task touched)
    // doesn't overwrite the current-folder counts.
    connect(_fileModel, &FileModel::folderRefreshed, this,
            [this](const QString& path, bool changed) {
                if (!changed) return;
                FileItem* folder = currentFolder();
                if (!folder || folder == _fileModel->rootItem()) return;
                if (QDir::cleanPath(folder->fileInfo().filePath())
                        != QDir::cleanPath(path)) return;
                updateStatusBar(folder);
            });

    // Tasks dock visibility model:
    //   - View → Tasks menu = persistent intent (sticky across runs).
    //   - Status-bar widget click = transient toggle (does NOT touch the
    //     menu).
    //   - Dock X-close = sets persistent intent off (syncs menu).
    //   - Conflict question posed = force-show the dock so the user can
    //     answer; doesn't touch the menu, so it's still treated as a
    //     transient open (subject to the auto-hide-on-finish rule).
    //   - When all tasks finish AND menu is unchecked AND dock is open,
    //     auto-hide the dock so the status-bar peek doesn't linger.
    connect(_pixee->taskManager(), &TaskManager::taskQuestionPosed,
            this, [this](QUuid, int, QVariantMap) {
                if (_taskDockWidget->isVisible()) return;
                _suppressDockVisibilitySync = true;
                _taskDockWidget->setVisible(true);
                _taskDockWidget->raise();
            });
    connect(_pixee->taskManager(), &TaskManager::groupRemoved,
            this, [this](QUuid) {
                if (_pixee->taskManager()->hasGroups()) return;
                if (_userTasksDockEnabled) return;
                if (!_taskDockWidget->isVisible()) return;
                _suppressDockVisibilitySync = true;
                _taskDockWidget->hide();
            });

    setCentralWidget(_centerStack);

    // Read persistent intent BEFORE createMenus so the View → Tasks
    // checkbox initialises correctly.
    {
        QSettings s;
        _userTasksDockEnabled = s.value("tasksDockEnabled", false).toBool();
    }

    // Menu bar + status bar
    createMenus();
    statusBar();  // force-create so it appears even when empty
    // Disable the bottom-right resize grip — the main window resizes
    // fine via its frame, and the grip shows up as a stray light line
    // at the right end of permanent widgets in the dark theme.
    statusBar()->setSizeGripEnabled(false);

    // Status-bar progress widget — right-aligned permanent. Click toggles
    // the dock without touching the menu (transient peek).
    _taskStatusWidget = new TaskStatusWidget(_pixee->taskManager(), this);
    statusBar()->addPermanentWidget(_taskStatusWidget);
    connect(_taskStatusWidget, &TaskStatusWidget::toggleDockRequested,
            this, [this]() {
                _suppressDockVisibilitySync = true;
                const bool target = !_taskDockWidget->isVisible();
                _taskDockWidget->setVisible(target);
                if (target) _taskDockWidget->raise();
            });

    // Read settings

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

    // If a previous session's saved state left the folders dock detached
    // from any dock area (and not floating either), Qt won't know how to
    // show it again. Re-anchor it to the left so the menu toggle works.
    if (dockWidgetArea(_dockWidget) == Qt::NoDockWidgetArea
            && !_dockWidget->isFloating()) {
        addDockWidget(Qt::LeftDockWidgetArea, _dockWidget);
    }
    // Same NoDockWidgetArea fallback as the folders dock.
    if (dockWidgetArea(_taskDockWidget) == Qt::NoDockWidgetArea
            && !_taskDockWidget->isFloating()) {
        addDockWidget(Qt::BottomDockWidgetArea, _taskDockWidget);
    }
    // Override Qt's restored visibility with our persistent intent —
    // the source of truth lives in tasksDockEnabled, not the saved
    // QMainWindow state blob.
    _suppressDockVisibilitySync = true;
    _taskDockWidget->setVisible(_userTasksDockEnabled);

    // Path edit drives navigation on Enter.
    QObject::connect(_pathLineEdit, &QLineEdit::returnPressed,
                     this, &MainWindow::goToPathFromLineEdit);

    // Add events

    // When expand in folder tree: append files
    QObject::connect(
        _folderTreeView, &QTreeView::expanded, this, [=](const QModelIndex &folderIndex) {
            const QModelIndex fileIndex = _folderFilterModel->mapToSource(folderIndex);
            FileItem* fileItem = static_cast<FileItem*>(fileIndex.internalPointer());
            if (fileItem && fileItem->fileType() == FileType::Folder) {
                _fileModel->appendFileItems(fileItem->fileInfo().filePath(), fileItem);
            }
        }
    );

    // When change selection in folder tree: go to folder
    QObject::connect(
        _folderTreeView->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [=](const QItemSelection &selected, const QItemSelection &deselected __attribute__((unused))) {
            if (selected.indexes().isEmpty()) {
                return;
            }
            QModelIndex folderIndex = selected.indexes().first();
            QModelIndex fileIndex = _folderFilterModel->mapToSource(folderIndex);
            if (fileIndex.isValid()) {
                goToFolderByFileIndex(fileIndex);
            }
        }
    );

    // When double-clicked or Enter on a selected list item: navigate into
    // the folder, or open the viewer for an image.
    QObject::connect(
        _fileListView, &QListView::activated,
        this, [=](const QModelIndex& fileFilterIndex) {
            const QModelIndex fileIndex = _fileFilterModel->mapToSource(fileFilterIndex);
            if (!fileIndex.isValid()) return;
            FileItem* item = static_cast<FileItem*>(fileIndex.internalPointer());
            if (!item) return;
            if (item->fileType() == FileType::Folder) {
                goToFolderByFileIndex(fileIndex);
            } else if (item->fileType() == FileType::Image) {
                activateImage(item);
            } else if (item->fileType() == FileType::File) {
                // Hand off to the OS shell — opens with the user's default
                // app, or executes the file if it's a binary on Windows.
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(item->fileInfo().filePath()));
            }
        }
    );

    // Right-click on the central file list — copy / move / delete.
    QObject::connect(_fileListView, &QListView::customContextMenuRequested,
                     this, &MainWindow::showFileListContextMenu);

    // Restore the last path the user was viewing. expandPath does the
    // multi-step lazy load synchronously — fine on local disk, can stutter
    // briefly on slow shares, but only once at startup.
    const QString lastPath = settings.value("lastPath").toString();
    if (!lastPath.isEmpty()) {
        const QModelIndex sourceIdx = _fileModel->expandPath(lastPath);
        if (sourceIdx.isValid()) {
            FileItem* item = static_cast<FileItem*>(sourceIdx.internalPointer());
            navigateTo(item);
        } else {
            navigateTo(nullptr);
        }
    } else {
        navigateTo(nullptr);
    }
}

void MainWindow::goToFolderByFileIndex(const QModelIndex& fileIndex) {
    FileItem* fileItem = static_cast<FileItem*>(fileIndex.internalPointer());
    if (!fileItem || fileItem->fileType() != FileType::Folder) return;

    FileItem* target = fileItem;
    if (fileItem->fileInfo().fileName() == "..") {
        // ".." resolves to the current folder's parent. The current folder
        // is the ".." item's tree parent; its tree parent is the grandparent.
        target = fileItem->parent() ? fileItem->parent()->parent() : nullptr;
    }
    navigateTo(target);
}

void MainWindow::navigateTo(FileItem* item) {
    // Folder navigation always pulls the user out of viewer mode.
    if (_centerStack && _centerStack->currentIndex() != 0) {
        _centerStack->setCurrentIndex(0);
        _viewerWidget->clear();
        if (_dockWasVisible) _dockWidget->show();
    }
    _fileListView->selectionModel()->clear();
    if (!item || item == _fileModel->rootItem()) {
        // Drive list (synthetic root). Showing top-level needs an invalid root.
        _fileListView->setRootIndex(QModelIndex());
        _pathLineEdit->setText(QString());
        // Also drop the folder tree's current selection.
        QSignalBlocker block(_folderTreeView->selectionModel());
        _folderTreeView->selectionModel()->clearSelection();
        _folderTreeView->selectionModel()->clearCurrentIndex();
        updateStatusBar(nullptr);
        return;
    }
    _fileModel->appendFileItems(item->fileInfo().filePath(), item);
    // If a task touched this folder while we were elsewhere, the model is
    // serving stale cached children. Invalidate them now so the user sees
    // the post-task state on entry, not just after F5.
    const QString itemNorm = QDir::cleanPath(item->fileInfo().filePath());
    if (_staleDirs.remove(itemNorm)) {
        _fileModel->refreshFolder(item);
    }
    const QModelIndex sourceIdx = _fileModel->indexFor(item);
    const QModelIndex proxyIdx = _fileFilterModel->mapFromSource(sourceIdx);
    _fileListView->setRootIndex(proxyIdx);
    _pathLineEdit->setText(displayPath(item->fileInfo().filePath()));
    expandFolderTreeTo(item);
    updateStatusBar(item);
}

FileItem* MainWindow::currentFolder() const {
    const QModelIndex proxyIdx = _fileListView->rootIndex();
    if (!proxyIdx.isValid()) return nullptr;
    const QModelIndex sourceIdx = _fileFilterModel->mapToSource(proxyIdx);
    if (!sourceIdx.isValid()) return nullptr;
    return static_cast<FileItem*>(sourceIdx.internalPointer());
}

void MainWindow::refreshCurrentFolder() {
    FileItem* folder = currentFolder();
    if (!folder) return;
    // Async path: queues a background re-enumeration. The status bar
    // updates via the folderRefreshed signal once the result is applied
    // (Phase 5 wires that — Phase 1 leaves the bar momentarily stale on
    // the post-refresh tick, which is harmless since the count rarely
    // changes between F5 presses).
    _fileModel->requestRefreshFolder(folder);
}

void MainWindow::onTaskPathTouched(QString dir) {
    _touchedDirs.insert(dir);
    // (Re)start the debounce — keeps coalescing while a batch is still
    // landing. Once the burst stops, the timer fires once, we refresh.
    _touchedDirsTimer.start();
}

void MainWindow::onTouchedDirsRefreshDue() {
    FileItem* folder = currentFolder();
    const QString currentNorm = (folder && folder != _fileModel->rootItem())
            ? QDir::cleanPath(folder->fileInfo().filePath())
            : QString();

    bool refreshedCurrent = false;
    for (const QString& dir : _touchedDirs) {
        const QString dirNorm = QDir::cleanPath(dir);
        if (!currentNorm.isEmpty() && dirNorm == currentNorm) {
            if (!refreshedCurrent) {
                // Async — same reasoning as refreshCurrentFolder. The
                // user is already looking at this folder, so we want a
                // refresh, but not at the cost of blocking the GUI on a
                // slow share for a refresh that often produces no diff.
                _fileModel->requestRefreshFolder(folder);
                refreshedCurrent = true;
            }
        } else {
            // The user isn't looking at this folder right now. Mark it
            // stale so navigating back into it later forces a fresh read
            // instead of showing the model's cached pre-task contents.
            _staleDirs.insert(dirNorm);
        }
    }
    _touchedDirs.clear();
}

void MainWindow::updateViewerStatusBar(const QSize& size) {
    if (!size.isValid()) {
        statusBar()->clearMessage();
        return;
    }
    statusBar()->showMessage(tr("Width: %1 | Height: %2")
        .arg(size.width()).arg(size.height()));
}

void MainWindow::updateStatusBar(FileItem* folder) {
    if (!folder || folder == _fileModel->rootItem()) {
        statusBar()->clearMessage();
        return;
    }
    int images = 0;
    int folders = 0;
    for (int i = 0; i < folder->childCount(); ++i) {
        FileItem* c = folder->child(i);
        if (!c) continue;
        if (c->fileType() == FileType::Image) {
            ++images;
        } else if (c->fileType() == FileType::Folder
                   && c->fileInfo().fileName() != "..") {
            ++folders;
        }
    }
    statusBar()->showMessage(tr("Images: %1  |  Folders: %2").arg(images).arg(folders));
}

void MainWindow::createMenus() {
    QMenuBar* mb = menuBar();

    QMenu* fileMenu = mb->addMenu(tr("&File"));
    QAction* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    QMenu* viewMenu = mb->addMenu(tr("&View"));
    QAction* refreshAction = viewMenu->addAction(tr("&Refresh"));
    refreshAction->setShortcut(QKeySequence::Refresh);  // F5 on most platforms
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshCurrentFolder);

    viewMenu->addSeparator();
    // Manual QAction with two-way binding to the dock's visibility. We don't
    // use QDockWidget::toggleViewAction() because it disables itself any
    // time Qt thinks the dock isn't properly parented to the main window
    // (which can happen after a saveState/restoreState round-trip from a
    // weird previous run).
    QAction* foldersToggle = new QAction(tr("&Folders"), this);
    foldersToggle->setCheckable(true);
    foldersToggle->setChecked(_dockWidget->isVisible());
    connect(foldersToggle, &QAction::toggled, this, [this](bool on) {
        _dockWidget->setVisible(on);
        if (on) _dockWidget->raise();
    });
    // Sync the action's check state from the dock — but block the action's
    // toggled() signal while we do, otherwise minimising the window fires
    // visibilityChanged(false), which would re-enter the toggled lambda and
    // explicitly hide the dock. After restore the dock would then stay
    // hidden because we'd flipped its *intent*, not just its transient
    // on-screen state.
    connect(_dockWidget, &QDockWidget::visibilityChanged,
            this, [foldersToggle](bool visible) {
                const QSignalBlocker block(foldersToggle);
                foldersToggle->setChecked(visible);
            });
    viewMenu->addAction(foldersToggle);

    _tasksToggleAction = new QAction(tr("&Tasks"), this);
    _tasksToggleAction->setCheckable(true);
    _tasksToggleAction->setChecked(_userTasksDockEnabled);
    connect(_tasksToggleAction, &QAction::toggled, this, [this](bool on) {
        _userTasksDockEnabled = on;
        QSettings().setValue("tasksDockEnabled", on);
        _suppressDockVisibilitySync = true;
        _taskDockWidget->setVisible(on);
        if (on) _taskDockWidget->raise();
    });
    // Dock close via X — treat as a persistent intent flip. Status-bar
    // and auto-hide paths set _suppressDockVisibilitySync first so this
    // handler only runs for genuine user-driven changes.
    connect(_taskDockWidget, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
                if (_suppressDockVisibilitySync) {
                    _suppressDockVisibilitySync = false;
                    return;
                }
                _userTasksDockEnabled = visible;
                QSettings().setValue("tasksDockEnabled", visible);
                const QSignalBlocker block(_tasksToggleAction);
                _tasksToggleAction->setChecked(visible);
            });
    viewMenu->addAction(_tasksToggleAction);

    QMenu* helpMenu = mb->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About Pixee"),
        tr("<b>Pixee</b><br><br>An image manager built on Qt 6."
           "<br><a href=\"https://github.com/DynartInteractive/Pixee\">Pixee on GitHub</a>"));
}

void MainWindow::expandFolderTreeTo(FileItem* item) {
    if (!item || item == _fileModel->rootItem()) return;

    // Collect the chain root → leaf so we expand parents before the leaf.
    QList<FileItem*> chain;
    for (FileItem* it = item; it && it != _fileModel->rootItem(); it = it->parent()) {
        chain.prepend(it);
    }

    // The folder tree's selection model is what triggers goToFolderByFileIndex
    // via selectionChanged. Block it while we set the current index so the
    // navigation doesn't recurse back through this method.
    QSignalBlocker block(_folderTreeView->selectionModel());

    // Expand the *ancestors* but not the selected folder itself — leaving
    // the leaf collapsed lets cursor-up/down move between siblings without
    // forcibly unfolding each folder's children as you pass over it.
    for (int i = 0; i < chain.size() - 1; ++i) {
        FileItem* it = chain[i];
        const QModelIndex srcIdx = _fileModel->indexFor(it);
        const QModelIndex folderProxyIdx = _folderFilterModel->mapFromSource(srcIdx);
        if (folderProxyIdx.isValid()) {
            _folderTreeView->expand(folderProxyIdx);
        }
    }

    const QModelIndex leafProxy = _folderFilterModel->mapFromSource(_fileModel->indexFor(item));
    if (leafProxy.isValid()) {
        _folderTreeView->selectionModel()->setCurrentIndex(
            leafProxy, QItemSelectionModel::ClearAndSelect);
        _folderTreeView->scrollTo(leafProxy);
    }
}

QString MainWindow::displayPath(const QString& storedPath) const {
    if (storedPath.isEmpty()) return storedPath;
    if (!_pixee->config()->useBackslash()) return storedPath;
    QString out = storedPath;
    out.replace('/', '\\');
    return out;
}

void MainWindow::activateImage(FileItem* item) {
    if (!item || item->fileType() != FileType::Image) return;
    const QString path = item->fileInfo().filePath();

    // Build the ordered image list (folder contents in file-list display
    // order) and find where the activated image sits in it.
    buildViewerImageList(path);
    if (_viewerImagePaths.isEmpty()) return;

    // Hide the folder-tree dock for the duration of the viewer; remember
    // its visibility so dismiss restores it (or doesn't, if the user had
    // already closed it).
    _dockWasVisible = _dockWidget->isVisible();
    if (_dockWasVisible) _dockWidget->hide();
    _centerStack->setCurrentWidget(_viewerWidget);
    _viewerWidget->setFocus();

    showViewerImageAt(_viewerIndex);
}

void MainWindow::buildViewerImageList(const QString& currentPath) {
    _viewerImagePaths.clear();
    _viewerIndex = -1;
    const QModelIndex root = _fileListView->rootIndex();
    if (!root.isValid() || !_fileFilterModel) return;

    const int rows = _fileFilterModel->rowCount(root);
    for (int i = 0; i < rows; ++i) {
        const QModelIndex proxyIdx = _fileFilterModel->index(i, 0, root);
        if (!proxyIdx.isValid()) continue;
        const QModelIndex srcIdx = _fileFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* it = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!it || it->fileType() != FileType::Image) continue;
        const QString p = it->fileInfo().filePath();
        if (p == currentPath) _viewerIndex = _viewerImagePaths.size();
        _viewerImagePaths.append(p);
    }
    if (_viewerIndex < 0 && !_viewerImagePaths.isEmpty()) _viewerIndex = 0;
}

void MainWindow::showViewerImageAt(int index) {
    if (index < 0 || index >= _viewerImagePaths.size()) return;
    _viewerIndex = index;
    const QString path = _viewerImagePaths.at(index);

    // Cancel any in-flight load (current + preloads from the previous image)
    // and start a fresh batch keyed to the new version.
    const int taskVersion = _imageAbortVersion.fetchAndAddRelease(1) + 1;

    // Cache hit? Show the full-res instantly. setImage resets fit/zoom which
    // is the per-image expectation.
    auto cachedIt = _viewerImageCache.constFind(path);
    if (cachedIt != _viewerImageCache.constEnd()) {
        _viewerWidget->setImage(cachedIt.value());
        touchViewerCache(path);
        updateViewerStatusBar(cachedIt.value().size());
    } else {
        // Show whatever placeholder we have so the user sees something
        // immediately. Thumbnail first; otherwise the dark canvas.
        const QImage placeholder = _fileModel ? _fileModel->thumbnailFor(path) : QImage();
        if (!placeholder.isNull()) {
            _viewerWidget->setImage(placeholder);
        } else {
            _viewerWidget->clear();
        }
        // Don't set dimensions yet — the placeholder is a thumbnail, not the
        // real image. onImageLoaded fills it in once the full-res arrives.
        updateViewerStatusBar(QSize());
        emit requestImageLoad(path, taskVersion);
    }

    setWindowTitle(QStringLiteral("Pixee - %1").arg(displayPath(path)));

    // Always preload neighbours after the current image is queued, so
    // navigation feels instant when the user moves to one we've cached.
    preloadViewerNeighbors(index, taskVersion);
}

void MainWindow::preloadViewerNeighbors(int currentIndex, int taskVersion) {
    auto preload = [this, taskVersion](int idx) {
        if (idx < 0 || idx >= _viewerImagePaths.size()) return;
        const QString p = _viewerImagePaths.at(idx);
        if (_viewerImageCache.contains(p)) return;  // already cached
        emit requestImageLoad(p, taskVersion);
    };
    preload(currentIndex + 1);
    preload(currentIndex - 1);
}

void MainWindow::touchViewerCache(const QString& path) {
    _viewerCacheOrder.removeAll(path);
    _viewerCacheOrder.append(path);
}

void MainWindow::onImageLoaded(QString path, QImage image) {
    // Cache regardless of whether this is the current image — preloaded
    // neighbours go into the same pool, so navigating to them is instant.
    constexpr int kMaxViewerCacheSize = 5;  // current + a couple prev/next + slack
    _viewerImageCache.insert(path, image);
    touchViewerCache(path);
    while (_viewerCacheOrder.size() > kMaxViewerCacheSize) {
        const QString oldest = _viewerCacheOrder.takeFirst();
        _viewerImageCache.remove(oldest);
    }

    // If this is the currently-shown image, swap from placeholder to the
    // full-res while preserving the user's zoom / pan / rotation.
    if (_viewerIndex >= 0 && _viewerIndex < _viewerImagePaths.size()
            && path == _viewerImagePaths.at(_viewerIndex)) {
        _viewerWidget->updateImage(image);
        updateViewerStatusBar(image.size());
    }
}

void MainWindow::onImageLoadFailed(QString path) {
    qWarning() << "Viewer: load failed for" << path;
}

void MainWindow::onImageLoadAborted(QString /*path*/) {
    // Expected — happens when the user navigates between images faster
    // than the previous one finishes loading.
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) exitFullscreen();
    else                enterFullscreen();
}

void MainWindow::enterFullscreen() {
    if (isFullScreen()) return;
    // Snapshot chrome visibility so the user's "I closed the dock" state
    // isn't trampled when we restore.
    _fsMenuBarVisible   = menuBar()->isVisible();
    _fsStatusBarVisible = statusBar()->isVisible();
    _fsDockVisible      = _dockWidget->isVisible();
    menuBar()->hide();
    statusBar()->hide();
    _dockWidget->hide();
    showFullScreen();
}

void MainWindow::exitFullscreen() {
    if (!isFullScreen()) return;
    showNormal();
    if (_fsMenuBarVisible)   menuBar()->show();
    if (_fsStatusBarVisible) statusBar()->show();
    if (_fsDockVisible)      _dockWidget->show();
}

void MainWindow::showViewerContextMenu(const QPoint& pos) {
    if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
    const QString src = _viewerImagePaths.at(_viewerIndex);

    FileOpsMenuBuilder builder({src}, _pixee->taskManager(), this);
    builder.setAdvanceCallback([this]() { advanceViewerAfterRemoval(); });
    builder.setPasteDestination(QFileInfo(src).absolutePath());
    builder.setRenameCallback([this](const QString& path) { renameItemAt(path); });
    builder.setCreateFolderCallback(
        [this](const QString& parentDir) { createFolderIn(parentDir); });

    QMenu menu(this);
    QMenu* zoomMenu = menu.addMenu(tr("Zoom"));
    populateViewerZoomMenu(zoomMenu);
    menu.addSeparator();
    builder.populate(&menu);
    menu.exec(_viewerWidget->mapToGlobal(pos));
}

void MainWindow::populateViewerZoomMenu(QMenu* zoomMenu) {
    auto* viewer = _viewerWidget;

    auto addPlain = [&](const QString& text, void (ViewerWidget::*slot)()) {
        QAction* a = zoomMenu->addAction(text);
        QObject::connect(a, &QAction::triggered, viewer, slot);
        return a;
    };
    addPlain(tr("Zoom in") + QStringLiteral("\t+"),  &ViewerWidget::zoomIn);
    addPlain(tr("Zoom out") + QStringLiteral("\t-"), &ViewerWidget::zoomOut);
    zoomMenu->addSeparator();

    // One exclusive group across the three fit modes AND the percent
    // entries: picking 100% switches to NoFit and unchecks any fit
    // mode; picking Fit unchecks any percent. The group is parented
    // to the menu so it dies with it (the menu is rebuilt per show).
    auto* group = new QActionGroup(zoomMenu);
    group->setExclusive(true);

    auto addFit = [&](const QString& text, ViewerWidget::FitMode m) {
        QAction* a = zoomMenu->addAction(text);
        a->setCheckable(true);
        a->setActionGroup(group);
        a->setChecked(viewer->fitMode() == m);
        QObject::connect(a, &QAction::triggered, viewer,
                         [viewer, m]() { viewer->setFitMode(m); });
    };
    addFit(tr("No fit"),                                          ViewerWidget::FitMode::NoFit);
    addFit(tr("Fit image to window"),                             ViewerWidget::FitMode::Fit);
    addFit(tr("Fit image to window, large only") + QStringLiteral("\t/"),
                                                                  ViewerWidget::FitMode::FitLargeOnly);
    zoomMenu->addSeparator();

    const int currentPct = viewer->currentZoomPercent();  // 0 in any fit mode
    constexpr int kPercents[] = { 1600, 1200, 800, 600, 400, 200, 100, 75, 50, 25, 10 };
    for (int pct : kPercents) {
        QString text = QStringLiteral("%1%").arg(pct);
        if (pct == 100) text += QStringLiteral("\t*");
        QAction* a = zoomMenu->addAction(text);
        a->setCheckable(true);
        a->setActionGroup(group);
        a->setChecked(currentPct == pct);
        QObject::connect(a, &QAction::triggered, viewer,
                         [viewer, pct]() { viewer->setZoomPercent(pct); });
    }
    zoomMenu->addSeparator();

    QAction* lock = zoomMenu->addAction(tr("Lock zoom"));
    lock->setCheckable(true);
    lock->setChecked(viewer->lockZoom());
    QObject::connect(lock, &QAction::toggled, viewer, &ViewerWidget::setLockZoom);
}

void MainWindow::advanceViewerAfterRemoval() {
    if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;

    const int removedIdx = _viewerIndex;
    const QString removedPath = _viewerImagePaths.at(removedIdx);
    _viewerImagePaths.removeAt(removedIdx);

    // Drop the cached image and its LRU entry — the file is gone (or about
    // to be), no point keeping it around.
    _viewerImageCache.remove(removedPath);
    _viewerCacheOrder.removeAll(removedPath);

    if (_viewerImagePaths.isEmpty()) {
        dismissViewer();
        return;
    }
    // Show what's now at the same index; if we were on the tail, the new
    // tail is one shorter so clamp.
    const int newIdx = qMin(removedIdx, _viewerImagePaths.size() - 1);
    showViewerImageAt(newIdx);
}

void MainWindow::viewerPrev() {
    if (_viewerIndex > 0) showViewerImageAt(_viewerIndex - 1);
}

void MainWindow::navigateUp() {
    FileItem* cur = currentFolder();
    if (!cur) return;                         // already at the drive list
    navigateTo(cur->parent());                // parent == _rootItem → drive list
}

void MainWindow::viewerNext() {
    if (_viewerIndex >= 0 && _viewerIndex + 1 < _viewerImagePaths.size()) {
        showViewerImageAt(_viewerIndex + 1);
    }
}

void MainWindow::dismissViewer() {
    if (!_centerStack) return;
    // Esc / Enter dismissing the viewer also exits fullscreen if we got
    // there via F11 — single keystroke fully returns the user to browse mode.
    if (isFullScreen()) exitFullscreen();
    // Cancel any in-flight viewer load.
    _imageAbortVersion.fetchAndAddRelease(1);
    _centerStack->setCurrentIndex(0);     // back to the browser page
    _viewerWidget->clear();
    _viewerImagePaths.clear();
    _viewerIndex = -1;
    // Free the preload cache — those images can be tens of MB each.
    _viewerImageCache.clear();
    _viewerCacheOrder.clear();
    if (_dockWasVisible) _dockWidget->show();
    setWindowTitle("Pixee");
    // Restore the folder counts now that we're back in browse mode.
    updateStatusBar(currentFolder());
    _fileListView->setFocus();
}

void MainWindow::goToPathFromLineEdit() {
    const QString text = _pathLineEdit->text().trimmed();
    if (text.isEmpty() || text == "/") {
        navigateTo(nullptr);
        return;
    }
    const QModelIndex sourceIdx = _fileModel->expandPath(text);
    if (!sourceIdx.isValid()) {
        // Path doesn't exist or isn't a folder — leave the line edit alone
        // so the user can fix the typo.
        return;
    }
    FileItem* item = static_cast<FileItem*>(sourceIdx.internalPointer());
    navigateTo(item);
}

QSize MainWindow::sizeHint() const {
    return QSize(1280, 720); // Default size
}

void MainWindow::closeEvent(QCloseEvent* event __attribute__((unused))) {
    _pixee->exit();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::ActivationChange) return;
    if (!isActiveWindow()) return;

    // Refresh the current folder on every activation. Cheap by
    // construction now: requestRefreshFolder hands off to a background
    // worker, and onRefreshed short-circuits with no model signals when
    // the diff is empty (the common case for alt-tab returns). The
    // earlier "skip first activation" workaround and the singleShot
    // defer are no longer needed — both were just compensating for the
    // synchronous refresh that this codepath now bypasses.
    FileItem* folder = currentFolder();
    if (!folder || folder == _fileModel->rootItem()) return;
    _fileModel->requestRefreshFolder(folder);
}

void MainWindow::exit() {

    // Restore the browser layout before saving — saveState() captures the
    // visibility of the menu bar / status bar / dock widgets, so closing
    // mid-viewer (or fullscreen) would otherwise leave the next launch with
    // chrome hidden and no obvious way to bring it back.
    if (isFullScreen()) exitFullscreen();
    if (_centerStack && _centerStack->currentIndex() != 0) {
        _centerStack->setCurrentIndex(0);
        if (_dockWasVisible) _dockWidget->show();
    }

    // Write settings

    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());
    settings.setValue("lastPath", _pathLineEdit->text());

    // Stop the viewer's async loader thread cleanly.
    _imageAbortVersion.fetchAndAddRelease(1);
    _imageLoaderThread.quit();
    _imageLoaderThread.wait();
}

MainWindow::~MainWindow() {}

void MainWindow::showFileListContextMenu(const QPoint& pos) {
    const FileListView::Selection selection = _fileListView->selectionPaths();

    FileOpsMenuBuilder builder(selection.paths, _pixee->taskManager(), this);
    builder.setImageOpsEnabled(selection.imageOpsAllowed);
    builder.setRenameCallback([this](const QString& path) { renameItemAt(path); });
    builder.setCreateFolderCallback(
        [this](const QString& parentDir) { createFolderIn(parentDir); });
    // The drive list (synthetic root) isn't a real folder and shouldn't
    // accept a paste. currentFolder() returns the drive list as the
    // model's rootItem, which we filter out here.
    if (FileItem* folder = currentFolder()) {
        if (folder != _fileModel->rootItem()) {
            builder.setPasteDestination(folder->fileInfo().filePath());
        }
    }
    // No advance callback — the file list's selection clears naturally on
    // the post-task folder refresh.

    QMenu menu(this);
    builder.populate(&menu);
    if (menu.isEmpty()) return;
    menu.exec(_fileListView->viewport()->mapToGlobal(pos));
}

void MainWindow::copyFileListSelectionToClipboard() {
    FileOpsMenuBuilder::copyPathsToClipboard(_fileListView->selectionPaths().paths);
}

void MainWindow::copyViewedImageToClipboard() {
    if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
    FileOpsMenuBuilder::copyPathsToClipboard({_viewerImagePaths.at(_viewerIndex)});
}

void MainWindow::pasteIntoCurrentFolder() {
    FileItem* folder = currentFolder();
    if (!folder || folder == _fileModel->rootItem()) return;
    FileOpsMenuBuilder::pasteFromClipboardToFolder(
        folder->fileInfo().filePath(), _pixee->taskManager(), this);
}

void MainWindow::pasteIntoViewerImageFolder() {
    if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
    const QString dest = QFileInfo(_viewerImagePaths.at(_viewerIndex)).absolutePath();
    FileOpsMenuBuilder::pasteFromClipboardToFolder(
        dest, _pixee->taskManager(), this);
}

void MainWindow::renameItemAt(const QString& path) {
    FileItem* item = _fileModel->itemForPath(path);
    if (!item) {
        // Path isn't currently mapped — could happen for a viewer image
        // whose parent folder hasn't been enumerated under the model
        // (rare; viewer is normally launched from the file list which
        // forces enumeration first).
        Toast::show(this, tr("Cannot rename — item not found in model"),
                    Toast::Error);
        return;
    }
    const QFileInfo info = item->fileInfo();
    const QString currentName = info.fileName();
    const QString parentDir = info.absolutePath();
    const bool isFolder = (item->fileType() == FileType::Folder);

    RenameDialog dlg(currentName, parentDir, isFolder, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString newName = dlg.newName();
    if (newName.isEmpty()) return;  // unchanged-name treated as no-op cancel

    if (!_fileModel->renameItem(item, newName)) {
        Toast::show(this,
            tr("Could not rename \"%1\" — file may be in use or read-only")
                .arg(currentName),
            Toast::Error);
    }
}

void MainWindow::createFolderIn(const QString& parentDir) {
    if (parentDir.isEmpty()) return;
    FileItem* parent = _fileModel->itemForPath(parentDir);
    if (!parent) {
        Toast::show(this, tr("Cannot create folder — parent not in model"),
                    Toast::Error);
        return;
    }

    NewFolderDialog dlg(parentDir, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString name = dlg.newName();
    if (name.isEmpty()) return;

    FileItem* created = _fileModel->createFolder(parent, name);
    if (!created) {
        Toast::show(this,
            tr("Could not create folder \"%1\" — check permissions").arg(name),
            Toast::Error);
        return;
    }

    // If the user is currently viewing the parent folder in the file
    // list, select + scroll to the freshly created row so the user can
    // see it landed and can immediately F2-rename if they want a
    // different name later.
    if (currentFolder() == parent) {
        const QModelIndex srcIdx = _fileModel->indexFor(created);
        const QModelIndex proxyIdx = _fileFilterModel->mapFromSource(srcIdx);
        if (proxyIdx.isValid()) {
            _fileListView->setCurrentIndex(proxyIdx);
            _fileListView->scrollTo(proxyIdx);
        }
    }
}

void MainWindow::onPathRenamed(QString oldPath, QString newPath) {
    // Viewer's image-path list: rekey if the renamed file is in it.
    // For folder renames, also rekey any descendant viewer paths so a
    // viewer opened from inside the renamed folder keeps working
    // (paths get the parent prefix substituted).
    auto rewritePath = [&](QString p) -> QString {
        if (p == oldPath) return newPath;
        // Folder-prefix rewrite: only when oldPath is a strict prefix
        // followed by '/' so we don't catch sibling names that share a
        // prefix (e.g. "/foo" matching "/foobar").
        if (p.startsWith(oldPath + QLatin1Char('/'))) {
            return newPath + p.mid(oldPath.length());
        }
        return p;
    };
    for (QString& p : _viewerImagePaths) p = rewritePath(p);
    for (QString& p : _viewerCacheOrder) p = rewritePath(p);

    // Image cache: rebuild with rewritten keys, preserving the cached
    // QImages so the renamed image stays decoded under its new name.
    QHash<QString, QImage> rebuilt;
    rebuilt.reserve(_viewerImageCache.size());
    for (auto it = _viewerImageCache.constBegin(); it != _viewerImageCache.constEnd(); ++it) {
        rebuilt.insert(rewritePath(it.key()), it.value());
    }
    _viewerImageCache = std::move(rebuilt);

    // Path edit reflects the current folder; if the user is inside a
    // renamed folder, update the displayed path so it matches reality.
    const QString currentText = _pathLineEdit->text();
    const QString rewrittenCurrent = rewritePath(currentText);
    if (rewrittenCurrent != currentText) {
        _pathLineEdit->setText(rewrittenCurrent);
    }

    // Recent-destination QSettings: rekey if they pointed at the
    // renamed folder (or a descendant of it). Avoids dangling 'last
    // copy/move dest' shortcuts in the menu.
    QSettings settings;
    for (const char* key : { "lastCopyToPath", "lastMoveToPath" }) {
        const QString stored = settings.value(key).toString();
        if (stored.isEmpty()) continue;
        const QString rewritten = rewritePath(stored);
        if (rewritten != stored) settings.setValue(key, rewritten);
    }
}
