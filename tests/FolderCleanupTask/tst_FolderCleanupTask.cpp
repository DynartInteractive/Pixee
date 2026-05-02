#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QtTest>

#include "FolderCleanupTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstFolderCleanupTask : public QObject {
    Q_OBJECT

private slots:
    void empty_folder_is_removed();
    void non_empty_folder_is_left_in_place();
    void empty_root_path_is_refresh_marker_only();
    void missing_root_path_succeeds_no_op();
    void affected_dirs_includes_parent_root_and_extras();
};

namespace {

TaskGroup* makeGroup(FolderCleanupTask*& outTask,
                     const QString& root,
                     const QStringList& extras = {}) {
    auto* group = new TaskGroup(QStringLiteral("Cleanup"));
    outTask = new FolderCleanupTask(root, extras, group);
    group->addTask(outTask);
    return group;
}

}

// ---------------------------------------------------------------------------

void TstFolderCleanupTask::empty_folder_is_removed() {
    TaskTestFixture f;
    const QString root = f.path("empty-tree");
    QDir().mkpath(root + "/a/b/c");

    FolderCleanupTask* task = nullptr;
    TaskGroup* group = makeGroup(task, root);
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!QFileInfo(root).exists(),
             "empty root + empty subdirs should rmdir bottom-up to nothing");
}

void TstFolderCleanupTask::non_empty_folder_is_left_in_place() {
    // Best-effort: rmdir on a non-empty dir silently fails. Task still
    // completes successfully — the user-skipped files staying behind
    // is the expected outcome of a Move folder where some conflicts
    // were Skip'd.
    TaskTestFixture f;
    const QString root = f.path("with-leftovers");
    QDir().mkpath(root + "/sub");
    TestHelpers::writeBytes(root + "/sub/keep.bin", 4);

    FolderCleanupTask* task = nullptr;
    TaskGroup* group = makeGroup(task, root);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(QFileInfo(root).isDir());
    QVERIFY(QFile::exists(root + "/sub/keep.bin"));
}

void TstFolderCleanupTask::empty_root_path_is_refresh_marker_only() {
    // Pure refresh-marker mode (Copy folder uses this — no source root
    // to remove). Task should just complete and report the additional
    // refresh dirs via affectedDirs.
    TaskTestFixture f;
    const QString refreshDir = f.path("dest-root");
    QDir().mkpath(refreshDir);

    FolderCleanupTask* task = nullptr;
    TaskGroup* group = makeGroup(task, QString(), { refreshDir });
    QSignalSpy finishedSpy(task, &Task::finished);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(QFileInfo(refreshDir).isDir());
}

void TstFolderCleanupTask::missing_root_path_succeeds_no_op() {
    // Source root vanished mid-batch (e.g. deleted by another process).
    // The "_root not a dir" branch short-circuits to a clean completion.
    TaskTestFixture f;
    const QString missing = f.path("never-existed");

    FolderCleanupTask* task = nullptr;
    TaskGroup* group = makeGroup(task, missing);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
}

void TstFolderCleanupTask::affected_dirs_includes_parent_root_and_extras() {
    TaskTestFixture f;
    const QString root = f.path("parent/root");
    const QString extra = f.path("dest");
    QDir().mkpath(root);
    QDir().mkpath(extra);

    FolderCleanupTask* task = nullptr;
    TaskGroup* group = makeGroup(task, root, { extra });

    const QStringList dirs = task->affectedDirs();
    // Order is parent, root, then extras (matches the impl).
    QCOMPARE(dirs.size(), 3);
    QCOMPARE(QDir::cleanPath(dirs.at(0)), QDir::cleanPath(f.path("parent")));
    QCOMPARE(QDir::cleanPath(dirs.at(1)), QDir::cleanPath(root));
    QCOMPARE(QDir::cleanPath(dirs.at(2)), QDir::cleanPath(extra));

    delete group;
}

QTEST_GUILESS_MAIN(TstFolderCleanupTask)
#include "tst_FolderCleanupTask.moc"
