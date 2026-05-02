// Trash / Recycle Bin tests are explicitly out of scope: they would
// pollute the developer's bin on every run and the test is hard to make
// hermetic. Trash semantics are exercised manually. Everything here
// runs with toTrash=false to hit the hard-delete fallback path that
// the network-share / SMB callers also rely on.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QtTest>

#include "DeleteFileTask.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstDeleteFileTask : public QObject {
    Q_OBJECT

private slots:
    void single_file_hard_delete_succeeds();
    void folder_with_contents_removed_recursively();
    void missing_path_succeeds_as_noop();
    void affected_dirs_returns_parent();
};

namespace {

TaskGroup* makeGroup(DeleteFileTask*& outTask, const QString& path) {
    auto* group = new TaskGroup(QStringLiteral("Delete"));
    outTask = new DeleteFileTask(path, group, /*toTrash=*/false);
    group->addTask(outTask);
    return group;
}

}

// ---------------------------------------------------------------------------

void TstDeleteFileTask::single_file_hard_delete_succeeds() {
    TaskTestFixture f;
    const QString p = f.path("doomed.bin");
    TestHelpers::writeBytes(p, 4096);
    QVERIFY(QFile::exists(p));

    DeleteFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, p);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
    QVERIFY(!QFile::exists(p));
}

void TstDeleteFileTask::folder_with_contents_removed_recursively() {
    TaskTestFixture f;
    const QString folder = f.path("doomed-folder");
    QDir().mkpath(folder + "/sub/deeper");
    TestHelpers::writeBytes(folder + "/a.bin", 16);
    TestHelpers::writeBytes(folder + "/sub/b.bin", 16);
    TestHelpers::writeBytes(folder + "/sub/deeper/c.bin", 16);

    DeleteFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, folder);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!QFileInfo(folder).exists());
}

void TstDeleteFileTask::missing_path_succeeds_as_noop() {
    // Documented behaviour in DeleteFileTask::run: "Treat as a successful
    // no-op — the user wanted it gone, it's gone." Don't surface as a
    // failure; matches what callers (refresh-after-delete) expect.
    TaskTestFixture f;
    const QString p = f.path("never-existed.bin");

    DeleteFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, p);
    QSignalSpy finishedSpy(task, &Task::finished);
    QSignalSpy failedSpy(task, &Task::failed);
    const QUuid id = task->id();

    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished());

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(f.lastStateOf(id), int(Task::Completed));
}

void TstDeleteFileTask::affected_dirs_returns_parent() {
    TaskTestFixture f;
    const QString p = f.path("sub/file.bin");
    QDir().mkpath(f.path("sub"));
    TestHelpers::writeBytes(p, 8);

    DeleteFileTask* task = nullptr;
    TaskGroup* group = makeGroup(task, p);

    // affectedDirs is a pure accessor — safe to read before enqueue.
    const QStringList dirs = task->affectedDirs();
    QCOMPARE(dirs.size(), 1);
    QCOMPARE(QDir::cleanPath(dirs.first()),
             QDir::cleanPath(f.path("sub")));

    // Tear the group down without enqueuing — manager doesn't own it
    // yet so we delete it directly.
    delete group;
}

QTEST_GUILESS_MAIN(TstDeleteFileTask)
#include "tst_DeleteFileTask.moc"
