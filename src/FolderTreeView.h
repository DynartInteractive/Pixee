#ifndef FOLDERTREEVIEW_H
#define FOLDERTREEVIEW_H

#include <QPersistentModelIndex>
#include <QTreeView>

class FileFilterModel;
class TaskManager;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

class FolderTreeView : public QTreeView
{
    Q_OBJECT
public:
    FolderTreeView(FileFilterModel* folderFilterModel);

    // Wires the bits the drop handler needs without reaching back into
    // MainWindow: where to enqueue tasks, what widget to parent dialogs
    // and toasts to. Call once after construction.
    void setDropContext(TaskManager* taskManager, QWidget* dialogParent);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    FileFilterModel* _folderFilterModel;
    TaskManager* _taskManager = nullptr;
    QWidget* _dialogParent = nullptr;
    // Folder row under the cursor during a drag. Highlighted in
    // paintEvent and used as the drop target in dropEvent.
    QPersistentModelIndex _dropHoverIndex;
};

#endif // FOLDERTREEVIEW_H
