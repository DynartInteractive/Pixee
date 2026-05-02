#ifndef FOLDERTREEVIEW_H
#define FOLDERTREEVIEW_H

#include <QTreeView>

class FileFilterModel;
class TaskManager;
class QDragEnterEvent;
class QDragMoveEvent;
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
    void dropEvent(QDropEvent* event) override;

private:
    FileFilterModel* _folderFilterModel;
    TaskManager* _taskManager = nullptr;
    QWidget* _dialogParent = nullptr;
};

#endif // FOLDERTREEVIEW_H
