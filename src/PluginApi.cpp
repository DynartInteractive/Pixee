#include "PluginApi.h"

#include <QDebug>

PluginApi::PluginApi(QString pluginId, QObject* parent)
    : QObject(parent), _pluginId(std::move(pluginId)) {}

void PluginApi::addAction(const QString& title, const QJSValue& callback) {
    // A non-callable second argument is the most likely plugin-author
    // mistake (passing the result of a call instead of the function). Say so
    // rather than registering an action that silently does nothing.
    // One pre-built string rather than streamed arguments: qDebug/qWarning
    // insert a space between arguments, which puts one inside the bracket
    // ("[plugin hello ]").
    if (!callback.isCallable()) {
        qWarning().noquote() << QStringLiteral("[plugin %1] addAction(%2): "
                                               "callback is not a function")
                                    .arg(_pluginId, title);
        return;
    }
    if (title.trimmed().isEmpty()) {
        qWarning().noquote() << QStringLiteral("[plugin %1] addAction: empty title, ignored")
                                    .arg(_pluginId);
        return;
    }
    _actions.append(Action{title, callback});
    emit actionAdded();
}

QStringList PluginApi::selectedFiles() const {
    return _selectedFiles ? _selectedFiles() : QStringList();
}

QString PluginApi::currentFolder() const {
    return _currentFolder ? _currentFolder() : QString();
}

void PluginApi::log(const QString& message) const {
    qDebug().noquote() << QStringLiteral("[plugin %1] %2").arg(_pluginId, message);
}
