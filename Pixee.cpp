#include "Pixee.h"
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

#include "AppSettings.h"
#include "Config.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "TaskManager.h"
#include "MainWindow.h"

// APP_VERSION comes from the VERSION file via Pixee.pro. The fallback keeps
// non-qmake builds (e.g. an IDE that skips the DEFINES) compiling.
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

Pixee::Pixee(int argc, char** argv) : _argc(argc) {
    QCoreApplication::setOrganizationName("Dynart");
    QCoreApplication::setApplicationName("Pixee");
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // _argc, not the by-value parameter — see the comment in Pixee.h.
    _app = new QApplication(_argc, argv);

    // Must precede any widget construction (below) so tr() picks up the
    // chosen language while the UI is built.
    installTranslators();

    _config = new Config();
    _theme = new Theme(_config);
    _thumbnailCache = new ThumbnailCache(_config);
    _taskManager = new TaskManager(_config->taskWorkerCount());

    _mainWindow = new MainWindow(this);
    _mainWindow->create();

    _theme->apply(_mainWindow);
}

void Pixee::installTranslators() {
    // Saved language wins; empty means "follow the OS". QTranslator::load's
    // locale form falls back sensibly (Pixee_hu_HU -> Pixee_hu), and returns
    // false for a missing/empty catalogue, in which case the English source
    // strings show through.
    const QString code = QSettings().value(AppSettings::kLanguage).toString();
    const QLocale locale = code.isEmpty() ? QLocale::system() : QLocale(code);

    if (_translator.load(locale, "Pixee", "_", ":/i18n")) {
        _app->installTranslator(&_translator);
    }
    // Qt's own strings (standard dialog buttons, etc.), from the Qt install's
    // translations dir when present.
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (_qtTranslator.load(locale, "qtbase", "_", qtDir)) {
        _app->installTranslator(&_qtTranslator);
    }
}

int Pixee::run() {
    _mainWindow->show();
    return _app->exec();
}

void Pixee::exit() {
    _mainWindow->exit();
    QApplication::quit();
    // Drain the task manager BEFORE deleting the main window so any
    // in-flight task signals (progress / state changes) don't fire onto
    // a dangling dock widget.
    if (_taskManager) {
        _taskManager->shutdown();
    }
    delete _mainWindow;
    delete _taskManager;
    delete _thumbnailCache;
}

Theme* Pixee::theme() const {
    return _theme;
}

Config* Pixee::config() const {
    return _config;
}

ThumbnailCache* Pixee::thumbnailCache() const {
    return _thumbnailCache;
}

TaskManager* Pixee::taskManager() const {
    return _taskManager;
}
