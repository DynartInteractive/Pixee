#ifndef PIXEE_H
#define PIXEE_H

#include <QApplication>

#include "Config.h"

class MainWindow;
class TaskManager;
class Theme;
class ThumbnailCache;

class Pixee
{
public:
    Pixee(int argc, char** argv);
    int run();
    void exit();
    Config* config() const;
    Theme* theme() const;
    ThumbnailCache* thumbnailCache() const;
    TaskManager* taskManager() const;
private:
    // QApplication keeps an int& to the argc it was constructed with and
    // re-reads it whenever QCoreApplication::arguments() is called — which
    // Qt itself does from the X11 session-manager callback, long after our
    // constructor has returned. Holding argc here keeps that reference
    // valid for the application's whole lifetime. (Qt may also rewrite it
    // when it strips its own -platform/-style arguments.)
    int _argc;
    Config* _config;
    Theme* _theme;
    ThumbnailCache* _thumbnailCache;
    TaskManager* _taskManager;
    QApplication* _app;
    MainWindow* _mainWindow;
};

#endif // PIXEE_H
