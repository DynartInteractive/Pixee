#include "FileOpsMenuBuilder.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QString>
#include <QUrl>

#include "CopyFileTask.h"
#include "DeleteFileTask.h"
#include "FileOpsHelpers.h"
#include "FolderCleanupTask.h"
#include "MoveFileTask.h"
#include "OpenWithDialog.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "Toast.h"

using FileOpsHelpers::clipboardSaysCut;
using FileOpsHelpers::destIsSourceOrDescendant;
using FileOpsHelpers::dropEffectBytes;
using FileOpsHelpers::expandToFiles;
using FileOpsHelpers::isDriveRoot;
using FileOpsHelpers::Pair;

namespace {
constexpr const char* kLastCopyKey = "lastCopyToPath";
constexpr const char* kLastMoveKey = "lastMoveToPath";
}

FileOpsMenuBuilder::FileOpsMenuBuilder(QStringList sourcePaths,
                                       TaskManager* taskManager,
                                       QWidget* dialogParent,
                                       QObject* parent)
    : QObject(parent),
      _paths(std::move(sourcePaths)),
      _taskManager(taskManager),
      _dialogParent(dialogParent) {}

QString FileOpsMenuBuilder::summary(const QString& templateOne,
                                    const QString& templateMany,
                                    const QString& contextArg) const {
    if (_paths.size() == 1) {
        return templateOne.arg(QFileInfo(_paths.first()).fileName(), contextArg);
    }
    return templateMany.arg(_paths.size()).arg(contextArg);
}

QString FileOpsMenuBuilder::pickFolder(const QString& settingsKey, const QString& title) {
    QSettings settings;
    const QString last = settings.value(settingsKey).toString();
    const QString picked = QFileDialog::getExistingDirectory(
        _dialogParent, title, last,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!picked.isEmpty()) settings.setValue(settingsKey, picked);
    return picked;
}

void FileOpsMenuBuilder::populate(QMenu* menu) {
    if (!menu || !_taskManager) return;
    if (_paths.isEmpty() && _pasteDestination.isEmpty()) return;

    QSettings settings;
    const QMimeData* clip = QApplication::clipboard()->mimeData();

    auto addPaste = [this, clip](QMenu* m) {
        QAction* pasteAct = m->addAction(
            clipboardSaysCut(clip) ? tr("Paste (Move)") : tr("Paste"));
        pasteAct->setShortcut(QKeySequence::Paste);
        pasteAct->setEnabled(clip && clip->hasUrls());
        connect(pasteAct, &QAction::triggered, this, [this]() { doPaste(); });
    };

    // No selection (folder background right-click): Paste alone, nothing
    // else makes sense without a target.
    if (_paths.isEmpty()) {
        if (!_pasteDestination.isEmpty()) addPaste(menu);
        return;
    }

    // ---- Open with ----
    // Image-only — non-image / folder selections route through the OS
    // shell via double-click instead. Always shows at least a 'Configure...'
    // entry so the user can discover the dialog before adding any programs.
    if (_imageOpsEnabled) {
        QMenu* openMenu = menu->addMenu(tr("Open with"));
        const QList<OpenWithProgram> programs = OpenWithDialog::loadPrograms();
        const QStringList paths = _paths;
        QWidget* dialogParent = _dialogParent;
        for (const OpenWithProgram& p : programs) {
            QAction* a = openMenu->addAction(p.label);
            const OpenWithProgram pCopy = p;
            connect(a, &QAction::triggered, this, [pCopy, paths, dialogParent]() {
                OpenWithDialog::launch(pCopy, paths, dialogParent);
            });
        }
        if (!programs.isEmpty()) openMenu->addSeparator();
        QAction* configAct = openMenu->addAction(tr("Configure..."));
        connect(configAct, &QAction::triggered, this, [dialogParent]() {
            OpenWithDialog dlg(dialogParent);
            dlg.exec();
        });
        menu->addSeparator();
    }

    // ---- Copy + Paste (system clipboard) ----
    // Pasting in Explorer creates a copy of the file at the destination
    // (CF_HDROP semantics on Windows); pasting in a text editor gives the
    // path(s) as text — Qt's setUrls populates both representations.
    QAction* clipCopy = menu->addAction(tr("Copy"));
    clipCopy->setShortcut(QKeySequence::Copy);  // visual hint; the real
    // window-wide handling lives on QShortcuts attached to the file list
    // and the viewer in MainWindow — actions inside an exec'd context menu
    // only have their shortcut active while the menu is open.
    connect(clipCopy, &QAction::triggered, this, [this]() { doCopyToClipboard(); });
    if (!_pasteDestination.isEmpty()) addPaste(menu);

    menu->addSeparator();

    // ---- Copy ----
    const QString lastCopy = settings.value(kLastCopyKey).toString();
    if (!lastCopy.isEmpty()) {
        QAction* a = menu->addAction(tr("Copy to \"%1\"").arg(QFileInfo(lastCopy).fileName()));
        connect(a, &QAction::triggered, this, [this, lastCopy]() { doCopy(lastCopy); });
    }
    QAction* copyPick = menu->addAction(tr("Copy to..."));
    connect(copyPick, &QAction::triggered, this, [this]() {
        const QString dest = pickFolder(kLastCopyKey, tr("Copy to..."));
        if (!dest.isEmpty()) doCopy(dest);
    });

    // ---- Move ----
    const QString lastMove = settings.value(kLastMoveKey).toString();
    if (!lastMove.isEmpty()) {
        QAction* a = menu->addAction(tr("Move to \"%1\"").arg(QFileInfo(lastMove).fileName()));
        connect(a, &QAction::triggered, this, [this, lastMove]() { doMove(lastMove); });
    }
    QAction* movePick = menu->addAction(tr("Move to..."));
    connect(movePick, &QAction::triggered, this, [this]() {
        const QString dest = pickFolder(kLastMoveKey, tr("Move to..."));
        if (!dest.isEmpty()) doMove(dest);
    });

    menu->addSeparator();

    // ---- Delete ----
    QAction* deleteAct = menu->addAction(tr("Delete"));
    connect(deleteAct, &QAction::triggered, this, [this]() { doDelete(/*toTrash=*/true); });
}

void FileOpsMenuBuilder::doCopyToClipboard() {
    copyPathsToClipboard(_paths);
}

void FileOpsMenuBuilder::doPaste() {
    pasteFromClipboardToFolder(_pasteDestination, _taskManager, _dialogParent);
}

void FileOpsMenuBuilder::pasteFromClipboardToFolder(const QString& destFolder,
                                                    TaskManager* taskManager,
                                                    QWidget* dialogParent) {
    if (destFolder.isEmpty() || !taskManager) return;
    const QMimeData* clip = QApplication::clipboard()->mimeData();
    if (!clip || !clip->hasUrls()) return;

    const bool wasCut = clipboardSaysCut(clip);
    handleDropOrPaste(clip, destFolder, /*forceMove=*/false, taskManager, dialogParent);

    // After a Cut+Paste, the clipboard's source paths are stale. Mirror
    // Explorer behaviour and clear it so a second paste doesn't try to
    // move-from-already-gone. Only relevant on the clipboard path — drop
    // handlers use a transient QMimeData owned by the drag.
    if (wasCut) {
        QApplication::clipboard()->clear();
    }
}

void FileOpsMenuBuilder::handleDropOrPaste(const QMimeData* mime,
                                           const QString& destFolder,
                                           bool forceMove,
                                           TaskManager* taskManager,
                                           QWidget* dialogParent) {
    if (destFolder.isEmpty() || !taskManager) return;
    if (!mime || !mime->hasUrls()) return;

    const bool isMove = forceMove || clipboardSaysCut(mime);
    const QString destNorm = QDir::cleanPath(QDir(destFolder).absolutePath());

    QStringList sourcePaths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;  // skip http/data/etc. URLs
        const QString path = url.toLocalFile();
        // Silent same-folder skip — dragging a file onto the folder it
        // already lives in (e.g. list selection onto the tree node of
        // the currently-viewed folder) is almost always an accidental
        // gesture. No conflict prompt, no Toast, no TaskGroup row for
        // these. Mixed selections still process the remaining sources.
        const QString parentNorm = QDir::cleanPath(QFileInfo(path).absolutePath());
        if (parentNorm == destNorm) continue;
        sourcePaths.append(path);
    }
    if (sourcePaths.isEmpty()) return;

    // Reject drive roots and descendant-pastes up-front — same protection
    // as direct Copy / Move. (Pasting a folder into itself or one of its
    // own subfolders would have expandToFiles mkpath new dirs inside the
    // tree it's still walking — a recursion landmine.)
    QStringList rejectedRoots;
    QStringList rejectedDescendants;
    QStringList accepted;
    for (const QString& s : sourcePaths) {
        if (isDriveRoot(s)) { rejectedRoots.append(s); continue; }
        if (QFileInfo(s).isDir() && destIsSourceOrDescendant(destFolder, s)) {
            rejectedDescendants.append(QFileInfo(s).fileName());
            continue;
        }
        accepted.append(s);
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(dialogParent,
            QObject::tr("Refusing to paste a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }
    if (!rejectedDescendants.isEmpty()) {
        Toast::show(dialogParent,
            QObject::tr("Cannot paste %1 into itself or a subfolder")
                .arg(rejectedDescendants.join(", ")),
            Toast::Error);
    }
    if (accepted.isEmpty()) return;

    QList<Pair> pairs;
    QStringList folderRoots;
    for (const QString& src : accepted) {
        if (QFileInfo(src).isDir()) folderRoots.append(src);
        pairs.append(expandToFiles(src, destFolder));
    }
    if (pairs.isEmpty() && folderRoots.isEmpty()) return;

    auto* group = new TaskGroup(isMove
        ? QObject::tr("Move %1 file(s) to \"%2\"")
              .arg(pairs.size()).arg(QDir(destFolder).dirName())
        : QObject::tr("Paste %1 file(s) to \"%2\"")
              .arg(pairs.size()).arg(QDir(destFolder).dirName()));

    for (const Pair& p : pairs) {
        if (isMove) group->addTask(new MoveFileTask(p.src, p.dst, group));
        else        group->addTask(new CopyFileTask(p.src, p.dst, group));
    }
    if (isMove) {
        for (const QString& root : folderRoots) {
            group->addTask(new FolderCleanupTask(root, { destFolder }, group));
        }
    } else if (!folderRoots.isEmpty()) {
        group->addTask(new FolderCleanupTask(QString(), { destFolder }, group));
    }

    taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::enqueueDeleteForExternalMove(const QStringList& paths,
                                                      TaskManager* taskManager) {
    if (!taskManager || paths.isEmpty()) return;

    QStringList alive;
    for (const QString& src : paths) {
        if (QFileInfo::exists(src)) alive.append(src);
    }
    if (alive.isEmpty()) return;

    auto* group = new TaskGroup(alive.size() == 1
        ? QObject::tr("Move out: delete \"%1\"").arg(QFileInfo(alive.first()).fileName())
        : QObject::tr("Move out: delete %1 item(s)").arg(alive.size()));

    // toTrash=false: external Move-out semantic — the file moved away,
    // a recoverable trash copy at the source would contradict Move.
    for (const QString& src : alive) {
        group->addTask(new DeleteFileTask(src, group, /*toTrash=*/false));
    }
    taskManager->enqueueGroup(group);
}

QMimeData* FileOpsMenuBuilder::buildPathsMimeData(const QStringList& paths) {
    if (paths.isEmpty()) return nullptr;
    QList<QUrl> urls;
    QStringList textPaths;
    urls.reserve(paths.size());
    textPaths.reserve(paths.size());
    for (const QString& p : paths) {
        urls.append(QUrl::fromLocalFile(p));
        textPaths.append(QDir::toNativeSeparators(p));
    }
    auto* mime = new QMimeData();
    mime->setUrls(urls);
    // setUrls alone leaves text/plain empty — text editors that paste-as-text
    // (Sublime, VS Code, etc.) end up with nothing. Set the path(s) as
    // newline-separated native paths so Ctrl-V in those apps gives the path.
    mime->setText(textPaths.join('\n'));
    // Tag as DROPEFFECT_COPY explicitly so other apps that key off this
    // (Explorer, etc.) treat the payload unambiguously — symmetrical with
    // how we read the same flag in handleDropOrPaste for Cut.
    mime->setData(FileOpsHelpers::kDropEffectMime, dropEffectBytes(5));
    return mime;
}

void FileOpsMenuBuilder::copyPathsToClipboard(const QStringList& paths) {
    if (QMimeData* mime = buildPathsMimeData(paths)) {
        QApplication::clipboard()->setMimeData(mime);
    }
}

void FileOpsMenuBuilder::doCopy(const QString& destFolder) {
    QList<Pair> pairs;
    bool sawFolder = false;
    QStringList rejectedRoots;
    QStringList rejectedDescendants;
    for (const QString& src : _paths) {
        if (isDriveRoot(src)) { rejectedRoots.append(src); continue; }
        if (QFileInfo(src).isDir()) {
            if (destIsSourceOrDescendant(destFolder, src)) {
                rejectedDescendants.append(QFileInfo(src).fileName());
                continue;
            }
            sawFolder = true;
        }
        pairs.append(expandToFiles(src, destFolder));
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Refusing to copy a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }
    if (!rejectedDescendants.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Cannot copy %1 into itself or a subfolder")
                .arg(rejectedDescendants.join(", ")),
            Toast::Error);
    }
    if (pairs.isEmpty()) return;

    auto* group = new TaskGroup(summary(
        tr("Copy %1 to \"%2\""),
        tr("Copy %1 file(s) to \"%2\""),
        QDir(destFolder).dirName()));
    for (const Pair& p : pairs) {
        group->addTask(new CopyFileTask(p.src, p.dst, group));
    }
    if (sawFolder) {
        // Refresh the user-visible destination once the batch finishes —
        // the per-file tasks only report deeply-nested dest dirs, so the
        // top-level dest wouldn't refresh on its own.
        group->addTask(new FolderCleanupTask(QString(), { destFolder }, group));
    }
    _taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::doMove(const QString& destFolder) {
    QList<Pair> pairs;
    QStringList folderRoots;
    QStringList rejectedRoots;
    QStringList rejectedDescendants;
    for (const QString& src : _paths) {
        if (isDriveRoot(src)) { rejectedRoots.append(src); continue; }
        if (QFileInfo(src).isDir()) {
            if (destIsSourceOrDescendant(destFolder, src)) {
                rejectedDescendants.append(QFileInfo(src).fileName());
                continue;
            }
            folderRoots.append(src);
        }
        pairs.append(expandToFiles(src, destFolder));
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Refusing to move a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }
    if (!rejectedDescendants.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Cannot move %1 into itself or a subfolder")
                .arg(rejectedDescendants.join(", ")),
            Toast::Error);
    }
    if (pairs.isEmpty()) return;

    if (_advance) _advance();
    auto* group = new TaskGroup(summary(
        tr("Move %1 to \"%2\""),
        tr("Move %1 file(s) to \"%2\""),
        QDir(destFolder).dirName()));
    for (const Pair& p : pairs) {
        group->addTask(new MoveFileTask(p.src, p.dst, group));
    }
    // For each source folder, append a cleanup task that bottom-up
    // rmdirs empties (files the user chose 'Skip' on stay behind, since
    // their parent dir won't be empty). Plus refresh the destination
    // root once the batch finishes — it won't auto-refresh from the
    // deeply-nested per-file tasks alone.
    for (const QString& root : folderRoots) {
        group->addTask(new FolderCleanupTask(root, { destFolder }, group));
    }
    _taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::doDelete(bool toTrash) {
    QStringList rejectedRoots;
    QStringList paths;
    for (const QString& src : _paths) {
        if (isDriveRoot(src)) {
            rejectedRoots.append(src);
            continue;
        }
        paths.append(src);
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Refusing to delete a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }
    if (paths.isEmpty()) return;

    const QString verb = toTrash ? tr("Delete") : tr("Permanently delete");
    const QString title = toTrash ? tr("Delete") : tr("Permanently delete");
    const QString question = paths.size() == 1
        ? tr("%1 \"%2\"?").arg(verb, QFileInfo(paths.first()).fileName())
        : tr("%1 %2 selected items?").arg(verb).arg(paths.size());
    if (QMessageBox::question(_dialogParent, title, question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (_advance) _advance();
    auto* group = new TaskGroup(paths.size() == 1
        ? tr("%1 \"%2\"").arg(verb, QFileInfo(paths.first()).fileName())
        : tr("%1 %2 item(s)").arg(verb).arg(paths.size()));
    // One task per top-level path. DeleteFileTask handles folders
    // atomically (moveToTrash on the whole folder, or removeRecursively
    // on fallback) so the user's trash gets one entry per item rather
    // than per inner file.
    for (const QString& src : paths) {
        group->addTask(new DeleteFileTask(src, group, toTrash));
    }
    _taskManager->enqueueGroup(group);
}
