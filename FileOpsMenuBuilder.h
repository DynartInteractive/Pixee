#ifndef FILEOPSMENUBUILDER_H
#define FILEOPSMENUBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QMenu;
class QMimeData;
class QWidget;
class TaskManager;

// Builds the Copy / Move / Delete context menu shared between the file
// list and the viewer. Both call sites populate a QMenu via populate()
// and exec it themselves; the builder owns the per-action handlers,
// the conflict-aware task construction, and the shared 'last
// destination folder' persistence (lastCopyToPath / lastMoveToPath
// in QSettings — same keys for both menus, so a recent destination
// from one is one click away from the other).
//
// Image-type operations (Scale, Convert, ...) used to live here too;
// they were removed pending a plugin system that will repopulate an
// 'Image operations' submenu from external code.
class FileOpsMenuBuilder : public QObject
{
    Q_OBJECT
public:
    // Fires immediately before a Move or Delete task is enqueued. The
    // viewer uses it to advance off the about-to-go-away image; the file
    // list leaves it unset because its selection clears naturally on the
    // post-task folder refresh.
    using AdvanceFn = std::function<void()>;
    // Fires when the user picks Rename. Receives the single selected
    // source path; the caller (typically MainWindow) shows the rename
    // dialog and drives FileModel::renameItem from there. The builder
    // does not show a dialog itself — the caller owns model access.
    using RenameFn = std::function<void(const QString& sourcePath)>;
    // Fires when the user picks New folder. Receives the parent
    // directory (= the same path setPasteDestination was given); the
    // caller shows the dialog and drives FileModel::createFolder.
    using CreateFolderFn = std::function<void(const QString& parentDir)>;

    FileOpsMenuBuilder(QStringList sourcePaths,
                       TaskManager* taskManager,
                       QWidget* dialogParent,
                       QObject* parent = nullptr);

    void setAdvanceCallback(AdvanceFn fn) { _advance = std::move(fn); }
    void setRenameCallback(RenameFn fn) { _rename = std::move(fn); }
    void setCreateFolderCallback(CreateFolderFn fn) { _createFolder = std::move(fn); }
    // When false, the 'Open with' submenu is suppressed — the caller
    // has decided the selection contains something image-typed actions
    // can't sensibly operate on (a folder, a non-image file, ...).
    // Default true.
    void setImageOpsEnabled(bool enabled) { _imageOpsEnabled = enabled; }
    // If non-empty, populate() prepends a 'Paste' action that targets
    // this directory (caller's responsibility to make sure it's a real
    // folder the user can write to). Action is disabled when there's
    // nothing pasteable on the clipboard.
    void setPasteDestination(const QString& dir) { _pasteDestination = dir; }

    // Appends the Copy / Move / Delete actions onto `menu`. Caller
    // still owns the menu and is responsible for exec(). No-op if
    // there are no source paths.
    void populate(QMenu* menu);

    // Trigger the Delete action programmatically — same path as the
    // menu item, including confirmation dialog and advance callback.
    // Used by the Del / Shift+Del shortcuts. toTrash=false skips the
    // OS trash and hard-deletes (matches Explorer's Shift+Delete).
    void runDelete(bool toTrash = true) { doDelete(toTrash); }

    // Static helper so window-wide Ctrl+C handlers can put paths on the
    // clipboard with the same payload format the context menu uses
    // (CF_HDROP via setUrls + plain-text path list via setText).
    static void copyPathsToClipboard(const QStringList& paths);

    // Post-drop helper for external drag-out with MoveAction. Enqueues a
    // delete-only TaskGroup for the source paths (same expansion as the
    // Delete menu — folders → per-file DeleteFileTask + FolderCleanupTask)
    // so the move-out is completed at our end. No confirmation dialog:
    // the user already committed by holding Shift while releasing. Paths
    // that no longer exist (Explorer's optimized same-volume move uses
    // MoveFileEx and deletes the source itself) are silently skipped.
    static void enqueueDeleteForExternalMove(const QStringList& paths,
                                             TaskManager* taskManager);

    // Builds the MIME payload used by both clipboard Copy and drag-out:
    //   - setUrls(file://...)        — CF_HDROP on Windows (Explorer)
    //   - setText(\n-joined paths)   — for paste-as-text targets (editors)
    //   - "Preferred DropEffect" = 5 — explicit COPY tag for receivers
    //                                  that key off the Windows MIME
    // Caller owns the returned object (typically transferred to QDrag or
    // QClipboard). Returns nullptr on empty input.
    static QMimeData* buildPathsMimeData(const QStringList& paths);

    // Static helper so Ctrl+V handlers can paste without going through
    // the menu builder. Reads the system clipboard, detects Cut vs Copy
    // semantics (Windows 'Preferred DropEffect'), expands folder URLs
    // recursively, and enqueues one TaskGroup of CopyFileTask /
    // MoveFileTask. Drive-root URLs are refused with a Toast::Error.
    // No-op if the clipboard has no usable URLs.
    static void pasteFromClipboardToFolder(const QString& destFolder,
                                           TaskManager* taskManager,
                                           QWidget* dialogParent);

    // Shared core for clipboard-paste and drop-event handlers. Reads URLs
    // out of `mime`, applies the same drive-root + descendant guards as
    // the menu actions, expands folders, and enqueues one TaskGroup.
    // `forceMove` is set by drop handlers when QDropEvent::dropAction()
    // is MoveAction (the Qt-native signal); the helper still ORs in the
    // Windows 'Preferred DropEffect' MIME so external sources from
    // Explorer that signalled Cut also flow through Move.
    static void handleDropOrPaste(const QMimeData* mime,
                                  const QString& destFolder,
                                  bool forceMove,
                                  TaskManager* taskManager,
                                  QWidget* dialogParent);

private:
    void doCopyToClipboard();
    void doPaste();
    void doCopy(const QString& destFolder);
    void doMove(const QString& destFolder);
    void doDelete(bool toTrash);

    // Opens QFileDialog::getExistingDirectory; remembers the result under
    // the matching settings key. Returns empty string on cancel.
    QString pickFolder(const QString& settingsKey, const QString& title);

    QString summary(const QString& templateOne, const QString& templateMany,
                    const QString& contextArg) const;

    QStringList _paths;
    TaskManager* _taskManager;
    QWidget* _dialogParent;
    AdvanceFn _advance;
    RenameFn _rename;
    CreateFolderFn _createFolder;
    bool _imageOpsEnabled = true;
    QString _pasteDestination;
};

#endif // FILEOPSMENUBUILDER_H
