#include "PluginManager.h"

#include <QAction>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMenu>

namespace {
const char* kManifestName = "plugin.json";
const char* kDefaultEntry = "main.js";
}

PluginManager::PluginManager(QObject* parent) : QObject(parent) {}

PluginManager::~PluginManager() {
    _unload();
}

void PluginManager::_unload() {
    // Delete the engine before the API object: QJSEngine holds a JS-side
    // wrapper around the PluginApi, and the QJSValue callbacks the API is
    // holding are references into the engine's heap. Tearing the engine down
    // first invalidates them while the owner is still alive, rather than the
    // other way round.
    for (Loaded& l : _loaded) {
        delete l.engine;
        delete l.api;
    }
    _loaded.clear();
    _plugins.clear();
}

void PluginManager::load(const QStringList& searchPaths) {
    _unload();

    QStringList claimed;   // folder names already taken, first path wins
    for (const QString& root : searchPaths) {
        QDir dir(root);
        if (!dir.exists()) continue;
        const QStringList entries =
            dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& folderName : entries) {
            if (claimed.contains(folderName)) continue;
            const Plugin plugin = _readManifest(dir.absoluteFilePath(folderName));
            if (!plugin.isValid()) continue;
            claimed.append(folderName);
            _plugins.append(plugin);
        }
    }

    for (const Plugin& plugin : _plugins) {
        Loaded loaded;
        loaded.plugin = plugin;
        if (_evaluate(loaded)) {
            _loaded.append(loaded);
        } else {
            delete loaded.engine;
            delete loaded.api;
        }
    }

    if (!_plugins.isEmpty()) {
        qDebug().noquote() << "Plugins loaded:" << _loaded.count()
                           << "of" << _plugins.count() << "found";
    }
}

Plugin PluginManager::_readManifest(const QString& folder) const {
    const QString manifestPath = QDir(folder).absoluteFilePath(kManifestName);
    if (!QFile::exists(manifestPath)) return Plugin();   // not a plugin, no warning

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Plugin manifest unreadable:" << manifestPath;
        return Plugin();
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "Plugin manifest is not valid JSON:" << manifestPath
                             << "-" << err.errorString();
        return Plugin();
    }

    const QJsonObject obj = doc.object();
    Plugin plugin;
    plugin.id = QDir(folder).dirName();
    plugin.name = obj.value("name").toString(plugin.id);
    plugin.version = obj.value("version").toString();
    plugin.description = obj.value("description").toString();
    plugin.author = obj.value("author").toString();
    plugin.folder = folder;

    const QString entry = obj.value("entry").toString(QString::fromLatin1(kDefaultEntry));
    // Keep a plugin inside its own folder: a manifest is data we did not
    // write, and "entry": "../../something.js" should not reach outside.
    const QDir folderDir(folder);
    const QString entryPath = QDir::cleanPath(folderDir.absoluteFilePath(entry));
    if (!entryPath.startsWith(QDir::cleanPath(folderDir.absolutePath()) + "/")) {
        qWarning().noquote() << "Plugin" << plugin.id
                             << "entry escapes its folder, ignored:" << entry;
        return Plugin();
    }
    if (!QFile::exists(entryPath)) {
        qWarning().noquote() << "Plugin" << plugin.id << "entry not found:" << entryPath;
        return Plugin();
    }
    plugin.entryPath = entryPath;
    return plugin;
}

bool PluginManager::_evaluate(Loaded& loaded) {
    QFile file(loaded.plugin.entryPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << "Plugin" << loaded.plugin.id
                             << "entry unreadable:" << loaded.plugin.entryPath;
        return false;
    }
    const QString source = QString::fromUtf8(file.readAll());

    loaded.engine = new QJSEngine();
    loaded.engine->installExtensions(QJSEngine::ConsoleExtension);

    loaded.api = new PluginApi(loaded.plugin.id);
    loaded.api->setSelectedFilesProvider(_selectedFiles);
    loaded.api->setCurrentFolderProvider(_currentFolder);

    // CppOwnership: the engine would otherwise take the object and delete it
    // on garbage collection, which would take the registered QJSValue
    // callbacks with it while the manager still holds the Loaded entry.
    QJSEngine::setObjectOwnership(loaded.api, QJSEngine::CppOwnership);
    loaded.engine->globalObject().setProperty(
        "pixee", loaded.engine->newQObject(loaded.api));

    const QJSValue result = loaded.engine->evaluate(source, loaded.plugin.entryPath);
    if (result.isError()) {
        _reportError(loaded.plugin.id, "failed to load", result);
        return false;
    }
    return true;
}

void PluginManager::_reportError(const QString& pluginId, const QString& what,
                                 const QJSValue& error) const {
    qWarning().noquote()
        << "Plugin" << pluginId << what << "-"
        << error.property("name").toString() + ": " + error.property("message").toString()
        << "at" << error.property("fileName").toString() + ":"
               + error.property("lineNumber").toString();
}

int PluginManager::populateMenu(QMenu* menu) {
    if (!menu) return 0;

    // Count first: with a single contributing plugin the entries go straight
    // into the menu, because a one-item submenu is a pointless extra click.
    // With several, each plugin gets its own submenu so two plugins offering
    // a similarly named action stay tellable apart — and so the menu height
    // stays bounded, which the 1280x720 budget cares about.
    int contributing = 0;
    for (const Loaded& l : _loaded) {
        if (!l.api->actions().isEmpty()) contributing++;
    }
    if (contributing == 0) return 0;

    int added = 0;
    for (const Loaded& l : _loaded) {
        const QList<PluginApi::Action>& actions = l.api->actions();
        if (actions.isEmpty()) continue;

        QMenu* target = menu;
        if (contributing > 1) target = menu->addMenu(l.plugin.name);

        for (const PluginApi::Action& action : actions) {
            QAction* menuAction = target->addAction(action.title);
            const QString pluginId = l.plugin.id;
            // The callback is copied into the lambda. It is a reference into
            // this plugin's engine, which outlives the menu: _unload deletes
            // engines, and the caller rebuilds the menu on every load().
            QJSValue callback = action.callback;
            connect(menuAction, &QAction::triggered, this, [this, pluginId, callback]() mutable {
                const QJSValue result = callback.call();
                if (result.isError()) _reportError(pluginId, "action failed", result);
            });
            added++;
        }
    }
    return added;
}
