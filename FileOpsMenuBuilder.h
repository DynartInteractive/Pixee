#ifndef FILEOPSMENUBUILDER_H
#define FILEOPSMENUBUILDER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QMenu;
class QWidget;
class TaskManager;

// Builds the Copy / Move / Scale / Convert / Delete context menu shared
// between the file list and the viewer. Both call sites populate a
// QMenu via populate() and exec it themselves; the builder owns the
// per-action handlers, the conflict-aware task construction, and the
// shared 'last destination folder' persistence (lastCopyToPath /
// lastMoveToPath in QSettings — same keys for both menus, so a recent
// destination from one is one click away from the other).
class FileOpsMenuBuilder : public QObject
{
    Q_OBJECT
public:
    // Fires immediately before a Move or Delete task is enqueued. The
    // viewer uses it to advance off the about-to-go-away image; the file
    // list leaves it unset because its selection clears naturally on the
    // post-task folder refresh.
    using AdvanceFn = std::function<void()>;

    FileOpsMenuBuilder(QStringList sourcePaths,
                       TaskManager* taskManager,
                       QWidget* dialogParent,
                       QObject* parent = nullptr);

    void setAdvanceCallback(AdvanceFn fn) { _advance = std::move(fn); }

    // Appends the Copy / Move / Scale / Convert / Delete actions onto
    // `menu`. Caller still owns the menu and is responsible for exec().
    // No-op if there are no source paths.
    void populate(QMenu* menu);

private:
    void doCopy(const QString& destFolder);
    void doMove(const QString& destFolder);
    void doScale(int longestEdge);
    void doConvert(const QByteArray& format);
    void doDelete();

    // Opens QFileDialog::getExistingDirectory; remembers the result under
    // the matching settings key. Returns empty string on cancel.
    QString pickFolder(const QString& settingsKey, const QString& title);

    QString summary(const QString& templateOne, const QString& templateMany,
                    const QString& contextArg) const;

    QStringList _paths;
    TaskManager* _taskManager;
    QWidget* _dialogParent;
    AdvanceFn _advance;
};

#endif // FILEOPSMENUBUILDER_H
