#ifndef PIXEE_H
#define PIXEE_H

#include <QApplication>

class MainWindow;

class Config;
class Theme;

class Pixee
{
public:
    Pixee(int argc, char** argv);
    int run();
    void exit();
    Config* config() const;
    Theme* theme() const;
private:
    Config* _config;
    Theme* _theme;
    QApplication* _app;
    MainWindow* _mainWindow;
};

#endif // PIXEE_H
