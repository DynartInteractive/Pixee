#include "FileOpsMenuBuilder.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
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
#include "MoveFileTask.h"
#include "ScaleImageTask.h"
#include "TaskGroup.h"
#include "TaskManager.h"

namespace {
constexpr const char* kLastCopyKey = "lastCopyToPath";
constexpr const char* kLastMoveKey = "lastMoveToPath";

const int kScalePresets[] = { 1024, 1920, 2560, 4096 };
const QList<QByteArray> kConvertFormats = { "jpg", "png", "webp" };
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
    if (!menu || _paths.isEmpty() || !_taskManager) return;

    QSettings settings;

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

    // ---- Convert ----
    QMenu* convertMenu = menu->addMenu(tr("Convert to..."));
    for (const QByteArray& fmt : kConvertFormats) {
        QAction* a = convertMenu->addAction(QString::fromLatin1(fmt).toUpper());
        const QByteArray fmtCopy = fmt;
        connect(a, &QAction::triggered, this, [this, fmtCopy]() { doConvert(fmtCopy); });
    }

    menu->addSeparator();

    // ---- Delete ----
    QAction* deleteAct = menu->addAction(tr("Delete"));
    connect(deleteAct, &QAction::triggered, this, [this]() { doDelete(); });
}

void FileOpsMenuBuilder::doCopyToClipboard() {
    copyPathsToClipboard(_paths);
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
    QApplication::clipboard()->setMimeData(mime);
}

void FileOpsMenuBuilder::doCopy(const QString& destFolder) {
    auto* group = new TaskGroup(summary(
        tr("Copy %1 to \"%2\""),
        tr("Copy %1 file(s) to \"%2\""),
        QDir(destFolder).dirName()));
    for (const QString& src : _paths) {
        const QString dst = QDir(destFolder).filePath(QFileInfo(src).fileName());
        group->addTask(new CopyFileTask(src, dst, group));
    }
    _taskManager->enqueueGroup(group);
}

void FileOpsMenuBuilder::doMove(const QString& destFolder) {
    if (_advance) _advance();
    auto* group = new TaskGroup(summary(
        tr("Move %1 to \"%2\""),
        tr("Move %1 file(s) to \"%2\""),
        QDir(destFolder).dirName()));
    for (const QString& src : _paths) {
        const QString dst = QDir(destFolder).filePath(QFileInfo(src).fileName());
        group->addTask(new MoveFileTask(src, dst, group));
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
    const QString question = _paths.size() == 1
        ? tr("Delete \"%1\"?").arg(QFileInfo(_paths.first()).fileName())
        : tr("Delete %1 selected files?").arg(_paths.size());
    if (QMessageBox::question(_dialogParent, tr("Delete"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (_advance) _advance();
    auto* group = new TaskGroup(_paths.size() == 1
        ? tr("Delete \"%1\"").arg(QFileInfo(_paths.first()).fileName())
        : tr("Delete %1 file(s)").arg(_paths.size()));
    for (const QString& p : _paths) {
        group->addTask(new DeleteFileTask(p, group));
    }
    _taskManager->enqueueGroup(group);
}
