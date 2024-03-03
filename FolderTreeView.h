#ifndef FOLDERTREEVIEW_H
#define FOLDERTREEVIEW_H

#include <QTreeView>

class FileFilterModel;

class FolderTreeView : public QTreeView
{
public:
    FolderTreeView(FileFilterModel* folderFilterModel);
};

#endif // FOLDERTREEVIEW_H
