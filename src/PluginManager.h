#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "Plugin.h"
#include "PluginApi.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QJSEngine;
class QMenu;

// Discovers, loads and runs JavaScript plugins.
//
// **One QJSEngine per plugin, not one shared engine.** A shared engine would
// let any plugin overwrite another's globals (or the `pixee` object itself),
// and an uncaught error would leave shared state half-modified. Engines are
// cheap enough at the scale we care about — a handful of plugins — and the
// isolation is worth far more than the memory.
//
// Everything runs on the GUI thread; see the note in PluginApi.h.
class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    // Host-side data the `pixee` object exposes. Set these before load().
    void setSelectedFilesProvider(PluginApi::PathsFn fn) { _selectedFiles = std::move(fn); }
    void setCurrentFolderProvider(PluginApi::PathFn fn) { _currentFolder = std::move(fn); }

    // Scans every path in Config::pluginSearchPaths(), first hit per folder
    // name wins, then evaluates each entry script. Safe to call again; it
    // tears down the previous engines first.
    void load(const QStringList& searchPaths);

    // Fills `menu` with one entry per registered action, grouped by plugin
    // when more than one plugin contributed. Returns the number of actions
    // added, so the caller can hide an otherwise-empty menu.
    int populateMenu(QMenu* menu);

    const QList<Plugin>& plugins() const { return _plugins; }

private:
    struct Loaded {
        Plugin plugin;
        QJSEngine* engine = nullptr;
        PluginApi* api = nullptr;
    };

    void _unload();
    // Reads plugin.json in `folder`. Returns an invalid Plugin on any
    // problem, having already warned about it.
    Plugin _readManifest(const QString& folder) const;
    bool _evaluate(Loaded& loaded);
    // Turns a QJSValue error into a single readable warning line.
    void _reportError(const QString& pluginId, const QString& what,
                      const QJSValue& error) const;

    QList<Plugin> _plugins;
    QList<Loaded> _loaded;
    PluginApi::PathsFn _selectedFiles;
    PluginApi::PathFn _currentFolder;
};

#endif // PLUGINMANAGER_H
