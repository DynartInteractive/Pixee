#include "FolderTreeView.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

#include "FileFilterModel.h"
#include "FileItem.h"
#include "FileOpsMenuBuilder.h"
#include "Toast.h"

namespace {
bool dropHasLocalFile(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) return false;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) return true;
    }
    return false;
}

// Pixee policy: drop = copy by default; Shift forces move. Honour an
// already-set MoveAction (internal Qt drags signal Move that way).
Qt::DropAction pickDropAction(const QDropEvent* event) {
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        return Qt::MoveAction;
    }
    if (event->dropAction() == Qt::MoveAction) {
        return Qt::MoveAction;
    }
    return Qt::CopyAction;
}
}

FolderTreeView::FolderTreeView(FileFilterModel* folderFilterModel)
    : _folderFilterModel(folderFilterModel) {
    setObjectName("folderTreeView");
    setModel(folderFilterModel);
    setHeaderHidden(true);
    setAlternatingRowColors(true);

    // Drag-and-drop. Drop = copy by default; Shift forces move. Hover
    // over a collapsed folder during a drag for ~600 ms and Qt expands
    // it for us — fires the existing 'expanded' signal in MainWindow,
    // which lazy-loads the folder's contents. setDragEnabled here is
    // for outgoing drags from a folder selection (Phase 3).
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);  // see FileListView for the gotcha
    setDragEnabled(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setAutoExpandDelay(600);
    setAutoScroll(true);
}

void FolderTreeView::setDropContext(TaskManager* taskManager, QWidget* dialogParent) {
    _taskManager = taskManager;
    _dialogParent = dialogParent;
}

void FolderTreeView::dragEnterEvent(QDragEnterEvent* event) {
    if (!dropHasLocalFile(event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(pickDropAction(event));
    event->accept();
}

void FolderTreeView::dragMoveEvent(QDragMoveEvent* event) {
    if (!dropHasLocalFile(event->mimeData())) {
        event->ignore();
        return;
    }
    // Per-row targeting: only accept when the cursor is over an actual
    // folder row. Empty area / between-rows / past-end → ignore, which
    // gives the OS the "no-drop" cursor for that exact spot. Qt's auto-
    // expand timer keys off this same accept/ignore state.
    const QModelIndex proxyIdx = indexAt(event->position().toPoint());
    if (!proxyIdx.isValid()) {
        event->ignore();
        return;
    }
    const QModelIndex srcIdx = _folderFilterModel->mapToSource(proxyIdx);
    if (!srcIdx.isValid()) {
        event->ignore();
        return;
    }
    FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
    if (!item || item->fileType() != FileType::Folder) {
        event->ignore();
        return;
    }
    event->setDropAction(pickDropAction(event));
    event->accept();
}

void FolderTreeView::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);
    if (!selectionModel()) return;

    QStringList paths;
    QStringList rejectedRoots;
    const QModelIndexList sel = selectionModel()->selectedRows();
    for (const QModelIndex& proxyIdx : sel) {
        const QModelIndex srcIdx = _folderFilterModel->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;
        FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
        if (!item || item->fileType() != FileType::Folder) continue;
        const QString p = item->fileInfo().filePath();
        // Drive-root drag would mean "drag the entire drive" — symmetric
        // with the menu and paste guards.
        if (QFileInfo(p).isRoot()) {
            rejectedRoots.append(p);
            continue;
        }
        paths.append(p);
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Refusing to drag a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }
    if (paths.isEmpty()) return;

    QMimeData* mime = FileOpsMenuBuilder::buildPathsMimeData(paths);
    if (!mime) return;

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const Qt::DropAction result =
        drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);

    // External Move post-exec delete (see FileListView::startDrag).
    if (result == Qt::MoveAction && drag->target() == nullptr && _taskManager) {
        FileOpsMenuBuilder::enqueueDeleteForExternalMove(paths, _taskManager);
    }
}

void FolderTreeView::dropEvent(QDropEvent* event) {
    if (!dropHasLocalFile(event->mimeData()) || !_taskManager) {
        event->ignore();
        return;
    }
    // Re-resolve the index under the cursor at drop time — a slow drag
    // may have settled on a different row than the last dragMove tick.
    const QModelIndex proxyIdx = indexAt(event->position().toPoint());
    if (!proxyIdx.isValid()) {
        event->ignore();
        return;
    }
    const QModelIndex srcIdx = _folderFilterModel->mapToSource(proxyIdx);
    if (!srcIdx.isValid()) {
        event->ignore();
        return;
    }
    FileItem* item = static_cast<FileItem*>(srcIdx.internalPointer());
    if (!item || item->fileType() != FileType::Folder) {
        event->ignore();
        return;
    }

    const Qt::DropAction action = pickDropAction(event);
    const bool isMove = (action == Qt::MoveAction);

    FileOpsMenuBuilder::handleDropOrPaste(
        event->mimeData(), item->fileInfo().filePath(), isMove,
        _taskManager, _dialogParent);

    event->setDropAction(action);
    event->acceptProposedAction();
}
