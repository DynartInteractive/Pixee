#include "FileListView.h"

#include "FileModel.h"
#include "FileFilterModel.h"
#include "FileListViewDelegate.h"
#include "Theme.h"

FileListView::FileListView(Config* config, Theme* theme, FileFilterModel* fileFilterModel) {
    setModel(fileFilterModel);

    auto delegate = new FileListViewDelegate(config, theme, fileFilterModel, this);
    setItemDelegate(delegate);

    setObjectName("fileListView");
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setUniformItemSizes(true);
}

