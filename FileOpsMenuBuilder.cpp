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

#include "ConvertFormatTask.h"
#include "CopyFileTask.h"
#include "DeleteFileTask.h"
#include "FolderCleanupTask.h"
#include "MoveFileTask.h"
#include "ScaleImageTask.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "Toast.h"

namespace {
constexpr const char* kLastCopyKey = "lastCopyToPath";
constexpr const char* kLastMoveKey = "lastMoveToPath";

const int kScalePresets[] = { 1024, 1920, 2560, 4096 };
const QList<QByteArray> kConvertFormats = { "jpg", "png", "webp" };

// True if `path` is a filesystem root (a drive root on Windows, '/' on
// posix). We refuse to recursively copy / move / delete drive roots —
// the user almost certainly didn't intend to operate on the entire
// drive, and the cost of a mistake is too high.
bool isDriveRoot(const QString& path) {
    return QFileInfo(path).isRoot();
}

// True if `dest` is the same folder as `source` or a folder under it.
// Copying / moving / pasting into one of source's own descendants is a
// recursion landmine: expandToFiles mkpath's destination subdirs as it
// walks, and those new dirs land inside the source tree the iterator
// is still enumerating. Refuse up-front. Comparison is case-sensitive
// and uses cleanPath + absolutePath so 'D:/foo' matches 'D:\\foo' and
// 'D:/foo/.' matches 'D:/foo'.
bool destIsSourceOrDescendant(const QString& dest, const QString& source) {
    const QString d = QDir::cleanPath(QDir(dest).absolutePath());
    const QString s = QDir::cleanPath(QDir(source).absolutePath());
    if (d == s) return true;
    return d.startsWith(s + QLatin1Char('/'));
}

// Windows clipboard: 'Preferred DropEffect' is a 4-byte little-endian
// DWORD. 2 = DROPEFFECT_MOVE (Cut); 5 = DROPEFFECT_COPY. Qt surfaces it
// as a custom mime format. Other platforms have analogues (gnome-copied-
// files on Linux); we only handle the Windows one for now.
constexpr const char* kDropEffectMime = "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";

bool clipboardSaysCut(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(kDropEffectMime)) return false;
    const QByteArray data = mime->data(kDropEffectMime);
    if (data.size() < 4) return false;
    const quint32 effect = static_cast<quint8>(data.at(0))
                         | (static_cast<quint8>(data.at(1)) << 8)
                         | (static_cast<quint8>(data.at(2)) << 16)
                         | (static_cast<quint8>(data.at(3)) << 24);
    return effect == 2;
}

QByteArray dropEffectBytes(quint32 effect) {
    QByteArray b(4, '\0');
    b[0] = static_cast<char>(effect & 0xFF);
    b[1] = static_cast<char>((effect >> 8) & 0xFF);
    b[2] = static_cast<char>((effect >> 16) & 0xFF);
    b[3] = static_cast<char>((effect >> 24) & 0xFF);
    return b;
}

// A (source file, destination file) pair. Recursive expansion produces
// many of these from a single folder source.
struct Pair {
    QString src;
    QString dst;
};

// Expand `src` (a file or folder) into a list of file-level (src, dst)
// pairs under `destBase`. For folders, replicates the directory tree at
// `destBase / <folderName> / ...`, mkpath'ing empty subdirectories so
// they survive the operation. Symlinks are skipped.
QList<Pair> expandToFiles(const QString& src, const QString& destBase) {
    QList<Pair> out;
    const QFileInfo info(src);
    if (!info.exists()) return out;

    if (info.isFile()) {
        out.append({ src, QDir(destBase).filePath(info.fileName()) });
        return out;
    }
    if (!info.isDir()) return out;

    const QString folderName = info.fileName();
    const QString folderDest = QDir(destBase).filePath(folderName);
    QDir().mkpath(folderDest);

    // Replicate the directory structure first so empty subfolders are
    // preserved at the destination.
    QDirIterator dirIt(src,
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        const QString d = dirIt.next();
        const QString rel = QDir(src).relativeFilePath(d);
        QDir().mkpath(QDir(folderDest).filePath(rel));
    }

    QDirIterator fileIt(src,
        QDir::Files | QDir::Hidden | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        const QString f = fileIt.next();
        const QString rel = QDir(src).relativeFilePath(f);
        out.append({ f, QDir(folderDest).filePath(rel) });
    }
    return out;
}
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

    // ---- Paste (clipboard → current folder) ----
    if (!_pasteDestination.isEmpty()) {
        const QMimeData* clip = QApplication::clipboard()->mimeData();
        QAction* pasteAct = menu->addAction(
            clipboardSaysCut(clip) ? tr("Paste (Move)") : tr("Paste"));
        pasteAct->setShortcut(QKeySequence::Paste);
        pasteAct->setEnabled(clip && clip->hasUrls());
        connect(pasteAct, &QAction::triggered, this, [this]() { doPaste(); });
        if (!_paths.isEmpty()) menu->addSeparator();
    }

    if (_paths.isEmpty()) return;

    // ---- Copy to system clipboard ----
    // Pasting in Explorer creates a copy of the file at the destination
    // (CF_HDROP semantics on Windows); pasting in a text editor gives the
    // path(s) as text — Qt's setUrls populates both representations.
    QAction* clipCopy = menu->addAction(tr("Copy"));
    clipCopy->setShortcut(QKeySequence::Copy);  // visual hint; the real
    // window-wide handling lives on QShortcuts attached to the file list
    // and the viewer in MainWindow — actions inside an exec'd context menu
    // only have their shortcut active while the menu is open.
    connect(clipCopy, &QAction::triggered, this, [this]() { doCopyToClipboard(); });

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

    // ---- Scale ----
    QMenu* scaleMenu = menu->addMenu(tr("Scale to..."));
    for (int edge : kScalePresets) {
        QAction* a = scaleMenu->addAction(tr("%1 px (longest edge)").arg(edge));
        const int edgeCopy = edge;
        connect(a, &QAction::triggered, this, [this, edgeCopy]() { doScale(edgeCopy); });
    }
    scaleMenu->menuAction()->setEnabled(_imageOpsEnabled);

    // ---- Convert ----
    QMenu* convertMenu = menu->addMenu(tr("Convert to..."));
    for (const QByteArray& fmt : kConvertFormats) {
        QAction* a = convertMenu->addAction(QString::fromLatin1(fmt).toUpper());
        const QByteArray fmtCopy = fmt;
        connect(a, &QAction::triggered, this, [this, fmtCopy]() { doConvert(fmtCopy); });
    }
    convertMenu->menuAction()->setEnabled(_imageOpsEnabled);

    menu->addSeparator();

    // ---- Delete ----
    QAction* deleteAct = menu->addAction(tr("Delete"));
    connect(deleteAct, &QAction::triggered, this, [this]() { doDelete(); });
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

    const bool isCut = clipboardSaysCut(clip);

    QStringList sourcePaths;
    for (const QUrl& url : clip->urls()) {
        if (!url.isLocalFile()) continue;  // skip http/data/etc. URLs
        sourcePaths.append(url.toLocalFile());
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

    auto* group = new TaskGroup(isCut
        ? QObject::tr("Move %1 file(s) to \"%2\"")
              .arg(pairs.size()).arg(QDir(destFolder).dirName())
        : QObject::tr("Paste %1 file(s) to \"%2\"")
              .arg(pairs.size()).arg(QDir(destFolder).dirName()));

    for (const Pair& p : pairs) {
        if (isCut) group->addTask(new MoveFileTask(p.src, p.dst, group));
        else       group->addTask(new CopyFileTask(p.src, p.dst, group));
    }
    if (isCut) {
        for (const QString& root : folderRoots) {
            group->addTask(new FolderCleanupTask(root, { destFolder }, group));
        }
    } else if (!folderRoots.isEmpty()) {
        group->addTask(new FolderCleanupTask(QString(), { destFolder }, group));
    }

    taskManager->enqueueGroup(group);

    // After a Cut+Paste, the clipboard's source paths are stale. Mirror
    // Explorer behaviour and clear it so a second paste doesn't try to
    // move-from-already-gone.
    if (isCut) {
        QApplication::clipboard()->clear();
    }
}

void FileOpsMenuBuilder::copyPathsToClipboard(const QStringList& paths) {
    if (paths.isEmpty()) return;
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
    // how we read the same flag in pasteFromClipboardToFolder for Cut.
    mime->setData(kDropEffectMime, dropEffectBytes(5));
    QApplication::clipboard()->setMimeData(mime);
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

void FileOpsMenuBuilder::doScale(int longestEdge) {
    auto* group = new TaskGroup(summary(
        tr("Scale %1 to %2 px"),
        tr("Scale %1 file(s) to %2 px"),
        QString::number(longestEdge)));
    for (const QString& src : _paths) {
        const QFileInfo info(src);
        const QString dst = QDir(info.absolutePath()).filePath(
            info.completeBaseName() + "_scaled." + info.suffix());
        group->addTask(new ScaleImageTask(src, dst, longestEdge, 92, group));
    }
    _taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::doConvert(const QByteArray& format) {
    const QString fmtUpper = QString::fromLatin1(format).toUpper();
    auto* group = new TaskGroup(summary(
        tr("Convert %1 to %2"),
        tr("Convert %1 file(s) to %2"),
        fmtUpper));
    for (const QString& src : _paths) {
        const QFileInfo info(src);
        const QString dst = QDir(info.absolutePath()).filePath(
            info.completeBaseName() + "." + QString::fromLatin1(format));
        group->addTask(new ConvertFormatTask(src, dst, format, 92, group));
    }
    _taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::doDelete() {
    QStringList rejectedRoots;
    for (const QString& src : _paths) {
        if (isDriveRoot(src)) rejectedRoots.append(src);
    }
    if (!rejectedRoots.isEmpty()) {
        Toast::show(_dialogParent,
            tr("Refusing to delete a drive root: %1").arg(rejectedRoots.join(", ")),
            Toast::Error);
    }

    QStringList paths;
    QStringList folderRoots;
    for (const QString& src : _paths) {
        if (isDriveRoot(src)) continue;
        if (QFileInfo(src).isDir()) folderRoots.append(src);
        paths.append(src);
    }
    if (paths.isEmpty()) return;

    const QString question = paths.size() == 1
        ? tr("Delete \"%1\"?").arg(QFileInfo(paths.first()).fileName())
        : tr("Delete %1 selected items?").arg(paths.size());
    if (QMessageBox::question(_dialogParent, tr("Delete"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (_advance) _advance();
    auto* group = new TaskGroup(paths.size() == 1
        ? tr("Delete \"%1\"").arg(QFileInfo(paths.first()).fileName())
        : tr("Delete %1 item(s)").arg(paths.size()));
    // Expand folders into per-file delete tasks. For a plain file source,
    // expandToFiles returns a single (src, src) pair — we just need the
    // src side.
    for (const QString& src : paths) {
        if (QFileInfo(src).isFile()) {
            group->addTask(new DeleteFileTask(src, group));
        } else {
            QDirIterator it(src,
                QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                QDirIterator::Subdirectories);
            while (it.hasNext()) {
                group->addTask(new DeleteFileTask(it.next(), group));
            }
        }
    }
    for (const QString& root : folderRoots) {
        group->addTask(new FolderCleanupTask(root, {}, group));
    }
    _taskManager->enqueueGroup(group);
}
