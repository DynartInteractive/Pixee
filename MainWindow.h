#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDockWidget>
#include <QLineEdit>
#include <QListView>
#include <QTreeView>

#include "FileModel.h"
#include "FileFilterModel.h"
#include "Pixee.h"

class FileItem;

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
    void showAbout();

private:
    void navigateTo(FileItem* item);
    void expandFolderTreeTo(FileItem* item);
    void createMenus();
    void updateStatusBar(FileItem* folder);
    QString displayPath(const QString& storedPath) const;
    FileItem* currentFolder() const;

    Pixee* _pixee;
    FileModel* _fileModel;
    FileFilterModel* _folderFilterModel;
    FileFilterModel* _fileFilterModel;
    QDockWidget* _dockWidget;
    QListView* _fileListView;
    QTreeView* _folderTreeView;
    QLineEdit* _pathLineEdit;
};
#endif // MAINWINDOW_H
