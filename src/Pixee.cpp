#include "Pixee.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
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

    // Ties the running window to its .desktop file. Without it Qt reports a
    // WM_CLASS of "Pixee", which does not match the desktop entry's id, so
    // Wayland compositors (and GNOME's dash) show the window under a generic
    // icon instead of ours — the icon lives in the desktop entry, not in the
    // window, on that platform. Harmless everywhere else.
    QGuiApplication::setDesktopFileName(QStringLiteral("net.dynart.Pixee"));

    // _argc, not the by-value parameter — see the comment in Pixee.h.
    _app = new QApplication(_argc, argv);

    // X11 and Windows read the icon off the window, so set it there too.
    // fromTheme picks up the installed hicolor icon; the qrc copy covers the
    // portable build, which installs nothing. Both are SVG, so this needs
    // Qt's svg image plugin — without it the icon is simply absent, the same
    // as the settings dialog's SVG icons.
    QIcon icon = QIcon::fromTheme(QStringLiteral("net.dynart.Pixee"));
    if (icon.isNull()) icon = QIcon(QStringLiteral(":/icons/app.svg"));
    if (!icon.isNull()) QApplication::setWindowIcon(icon);

    // Qt 6 refuses to decode any image over 256 MB decoded (about 64 MP at
    // 32 bpp) — ordinary for panoramas and high-end camera files. Raise it so
    // the viewer can open them; the loaders catch bad_alloc for the truly
    // impossible ones. Process-wide, so it covers every reader.
    QImageReader::setAllocationLimit(sizeof(void*) >= 8 ? 4096 : 1024);

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
    const int rc = _app->exec();

    // Tear down only after the event loop has returned. exit() runs inside
    // the main window's own closeEvent; deleting the window there unwound the
    // stack into a destroyed object (and, for File → Quit, into the menu and
    // action that triggered it).
    delete _mainWindow;      // also deletes the models (FileModel joins its threads)
    _mainWindow = nullptr;
    delete _taskManager;
    _taskManager = nullptr;
    delete _thumbnailCache;  // joins the decode pool and the DB thread
    _thumbnailCache = nullptr;
    delete _theme;
    delete _config;
    return rc;
}

void Pixee::exit() {
    _mainWindow->exit();
    // Drain the task manager BEFORE the main window goes away so any
    // in-flight task signals (progress / state changes) don't fire onto
    // a dangling dock widget.
    if (_taskManager) {
        _taskManager->shutdown();
    }
    QApplication::quit();
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
