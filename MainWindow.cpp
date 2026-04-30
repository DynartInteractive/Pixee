#include <QAction>
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
#include <QVBoxLayout>

#include "MainWindow.h"
#include "Config.h"
#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileModel.h"
#include "FolderTreeView.h"
#include "FileListView.h"
#include "ImageLoader.h"
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

    _folderFilterModel = new FileFilterModel();
    _folderFilterModel->setSourceModel(_fileModel);
    _folderFilterModel->setAcceptedFileTypes({ FileType::Loading, FileType::Folder });
    _folderFilterModel->setShowDotDot(false);
    _folderFilterModel->sort(0, Qt::AscendingOrder);

    _fileFilterModel = new FileFilterModel();
    _fileFilterModel->setSourceModel(_fileModel);
    _fileFilterModel->setAcceptedFileTypes({ FileType::Folder, FileType::Image });
    _fileFilterModel->setShowDotDot(true);
    _fileFilterModel->sort(0, Qt::AscendingOrder);

    // Widgets

    _folderTreeView = new FolderTreeView(
        _folderFilterModel
    );

    _fileListView = new FileListView(
        _pixee->config(),
        _pixee->theme(),
        _pixee->thumbnailCache(),
        _fileFilterModel
    );

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

    setCentralWidget(_centerStack);

    // Menu bar + status bar
    createMenus();
    statusBar();  // force-create so it appears even when empty

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
            }
        }
    );

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
    _fileModel->refreshFolder(folder);
    updateStatusBar(folder);
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
    connect(_dockWidget, &QDockWidget::visibilityChanged,
            foldersToggle, &QAction::setChecked);
    viewMenu->addAction(foldersToggle);

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
    } else {
        // Show whatever placeholder we have so the user sees something
        // immediately. Thumbnail first; otherwise the dark canvas.
        const QImage placeholder = _fileModel ? _fileModel->thumbnailFor(path) : QImage();
        if (!placeholder.isNull()) {
            _viewerWidget->setImage(placeholder);
        } else {
            _viewerWidget->clear();
        }
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

    QMenu menu(this);

    // "Copy to <last>" if we remember a previous destination.
    QSettings settings;
    const QString lastDest = settings.value("viewerLastCopyToPath").toString();
    if (!lastDest.isEmpty()) {
        QFileInfo lastInfo(lastDest);
        QAction* lastAct = menu.addAction(tr("Copy to %1").arg(lastInfo.fileName()));
        connect(lastAct, &QAction::triggered, this, [this, lastDest]() {
            copyCurrentImageTo(lastDest);
        });
    }
    QAction* pickAct = menu.addAction(tr("Copy to..."));
    connect(pickAct, &QAction::triggered, this, &MainWindow::pickAndCopyCurrentImage);

    menu.addSeparator();

    QAction* rotateLeftAct = menu.addAction(tr("Rotate left"));
    rotateLeftAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(rotateLeftAct, &QAction::triggered, _viewerWidget, &ViewerWidget::rotateLeft);

    QAction* rotateRightAct = menu.addAction(tr("Rotate right"));
    rotateRightAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(rotateRightAct, &QAction::triggered, _viewerWidget, &ViewerWidget::rotateRight);

    menu.exec(_viewerWidget->mapToGlobal(pos));
}

void MainWindow::pickAndCopyCurrentImage() {
    QSettings settings;
    const QString lastDest = settings.value("viewerLastCopyToPath").toString();
    const QString picked = QFileDialog::getExistingDirectory(
        this, tr("Copy to..."), lastDest,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) return;
    copyCurrentImageTo(picked);
}

void MainWindow::copyCurrentImageTo(const QString& destFolder) {
    if (_viewerIndex < 0 || _viewerIndex >= _viewerImagePaths.size()) return;
    const QString src = _viewerImagePaths.at(_viewerIndex);
    const QFileInfo srcInfo(src);
    const QString dst = QDir(destFolder).filePath(srcInfo.fileName());
    if (QFile::exists(dst)) {
        QMessageBox::warning(this, tr("Copy to..."),
            tr("A file named \"%1\" already exists in that folder.")
                .arg(srcInfo.fileName()));
        return;
    }
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, tr("Copy to..."),
            tr("Failed to copy %1 to %2.").arg(src, destFolder));
        return;
    }
    QSettings settings;
    settings.setValue("viewerLastCopyToPath", destFolder);
}

void MainWindow::viewerPrev() {
    if (_viewerIndex > 0) showViewerImageAt(_viewerIndex - 1);
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
