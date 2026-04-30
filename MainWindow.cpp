#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
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

    // Layout

    _dockWidget = new QDockWidget("Folders");
    _dockWidget->setObjectName("foldersDockWidget");
    _dockWidget->setWidget(_folderTreeView);
    _dockWidget->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, _dockWidget);

    auto w = new QWidget();
    w->setObjectName("centralWidget");
    auto v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    v->addWidget(_pathLineEdit);
    v->addWidget(_fileListView);
    setCentralWidget(w);

    // Menu bar + status bar
    createMenus();
    statusBar();  // force-create so it appears even when empty

    // Read settings

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

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

    // When double-clicked or Enter on a selected list item: go to folder.
    // `activated` covers both: it's the standard "open this item" trigger.
    QObject::connect(
        _fileListView, &QListView::activated,
        this, [=](const QModelIndex& fileFilterIndex) {
            const QModelIndex fileIndex = _fileFilterModel->mapToSource(fileFilterIndex);
            if (fileIndex.isValid()) {
                goToFolderByFileIndex(fileIndex);
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

    // Write settings

    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());
    settings.setValue("lastPath", _pathLineEdit->text());
}

MainWindow::~MainWindow() {}
