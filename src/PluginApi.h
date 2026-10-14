#ifndef PLUGINAPI_H
#define PLUGINAPI_H

#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// The object a plugin script sees as the global `pixee`. One instance per
// plugin, so every call already knows which plugin made it — that is what
// lets addAction() attribute a menu entry and log() prefix its output
// without the script having to identify itself.
//
// Everything here runs on the GUI thread. QJSEngine is not thread-safe and
// one engine belongs to one thread, so a future async API (HTTP, long jobs)
// has to hand work to a worker and call back *into this thread*, never touch
// the engine from another one. Same rule the rest of the app follows.
//
// The host-side getters are std::function rather than a pointer back to
// MainWindow on purpose: it keeps this class testable and stops the plugin
// layer from growing a dependency on the whole window.
class PluginApi : public QObject {
    Q_OBJECT
public:
    using PathsFn = std::function<QStringList()>;
    using PathFn = std::function<QString()>;

    struct Action {
        QString title;
        QJSValue callback;
    };

    explicit PluginApi(QString pluginId, QObject* parent = nullptr);

    void setSelectedFilesProvider(PathsFn fn) { _selectedFiles = std::move(fn); }
    void setCurrentFolderProvider(PathFn fn) { _currentFolder = std::move(fn); }

    const QList<Action>& actions() const { return _actions; }

    // --- the JS-visible surface -------------------------------------------
    // Registers a menu entry under Plugins. Called at load time; calling it
    // later works but the menu is only rebuilt when plugins are (re)loaded.
    Q_INVOKABLE void addAction(const QString& title, const QJSValue& callback);
    // Absolute paths of the current file-list selection. Empty when nothing
    // is selected, or when the viewer is up.
    Q_INVOKABLE QStringList selectedFiles() const;
    // Absolute path of the folder the file list is showing.
    Q_INVOKABLE QString currentFolder() const;
    // Writes to the application log, prefixed with the plugin id.
    Q_INVOKABLE void log(const QString& message) const;

signals:
    // Emitted when a script calls addAction, so the manager can notice a
    // registration that happens after the initial load.
    void actionAdded();

private:
    QString _pluginId;
    QList<Action> _actions;
    PathsFn _selectedFiles;
    PathFn _currentFolder;
};

#endif // PLUGINAPI_H
