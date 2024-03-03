#include "FolderTreeView.h"
#include "FileFilterModel.h"

#include <QTreeView>

FolderTreeView::FolderTreeView(FileFilterModel* folderFilterModel) {
    setObjectName("folderTreeView");
    setModel(folderFilterModel);
    setHeaderHidden(true);
    setAlternatingRowColors(true);

}
