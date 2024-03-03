#include "Pixee.h"
#include <QCoreApplication>
#include <QTranslator>

#include "Config.h"
#include "Theme.h"
#include "MainWindow.h"

Pixee::Pixee(int argc, char** argv) {
    QCoreApplication::setOrganizationName("Dynart");
    QCoreApplication::setApplicationName("Pixee");

    _app = new QApplication(argc, argv);

    _config = new Config();
    _theme = new Theme(_config);

    _mainWindow = new MainWindow(this);
    _mainWindow->create();

    _theme->apply(_mainWindow);
}

int Pixee::run() {

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "Pixee_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            _app->installTranslator(&translator);
            break;
        }
    }

    _mainWindow->show();

    return _app->exec();
}

void Pixee::exit() {
    _mainWindow->exit();
    QApplication::quit();
    delete _mainWindow;
}

Theme* Pixee::theme() const {
    return _theme;
}

Config* Pixee::config() const {
    return _config;
}
