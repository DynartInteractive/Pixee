#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QListView>

class Config;
class Theme;
class FileModel;
class FileFilterModel;


class FileListView : public QListView
{
public:
    FileListView(Config* config, Theme* theme, FileFilterModel* fileFilterModel);
private:
    Config* _config;
    Theme* _theme;
    FileModel* _fileModel;
};

#endif // FILELISTVIEW_H
