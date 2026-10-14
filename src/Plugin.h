#ifndef PLUGIN_H
#define PLUGIN_H

#include <QString>

// One discovered plugin, as described by its plugin.json. Pure data: the
// engine, the API object and the registered actions all live in
// PluginManager, keyed by `id`.
//
// `id` is the folder name, not a field in the manifest. That makes it
// impossible for two plugins in the same search path to claim the same id,
// and it means a plugin can be disabled by renaming its folder. The first
// search path that yields a given id wins (see Config::pluginSearchPaths),
// so a user copy shadows a bundled one of the same name.
struct Plugin {
    QString id;           // folder name, e.g. "hello"
    QString name;         // manifest "name", falls back to id
    QString version;      // manifest "version", may be empty
    QString description;  // manifest "description", may be empty
    QString author;       // manifest "author", may be empty
    QString folder;       // absolute path to the plugin folder
    QString entryPath;    // absolute path to the entry script

    bool isValid() const { return !id.isEmpty() && !entryPath.isEmpty(); }
};

#endif // PLUGIN_H
