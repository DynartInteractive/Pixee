#include <QStorageInfo>
#include <QLabel>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

#include "MainWindow.h"
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

    _fileModel = new FileModel(_pixee->theme());

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

    // Read settings

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

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

    // When double click item in list: go to folder
    QObject::connect(
        _fileListView, &QListView::doubleClicked,
        this, [=](const QModelIndex& fileFilterIndex) {
            const QModelIndex fileIndex = _fileFilterModel->mapToSource(fileFilterIndex);
            if (fileIndex.isValid()) {
                goToFolderByFileIndex(fileIndex);
            }
        }
    );

}

void MainWindow::goToFolderByFileIndex(const QModelIndex& fileIndex) {
    FileItem* fileItem = static_cast<FileItem*>(fileIndex.internalPointer());
    if (fileItem && fileItem->fileType() == FileType::Folder) {
        _fileListView->selectionModel()->clear();
        if (fileItem->fileInfo().fileName() == "..") {
            _fileModel->appendFileItems(fileItem->parent()->fileInfo().filePath(), fileItem->parent());
        } else {
            _fileModel->appendFileItems(fileItem->fileInfo().filePath(), fileItem);
        }
        QModelIndex fileRootIndex = _fileFilterModel->mapFromSource(fileIndex);
        _fileListView->setRootIndex(fileRootIndex);
    }
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
}

MainWindow::~MainWindow() {}
