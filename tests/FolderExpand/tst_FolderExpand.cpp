// expandToFiles is unit-tested in Phase 2 (tst_FileOpsHelpers); this
// suite verifies the integration: expanded pairs feed CopyFileTask
// instances through a real TaskManager and the resulting on-disk
// structure mirrors the source.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtTest>

#include "CopyFileTask.h"
#include "FileOpsHelpers.h"
#include "Task.h"
#include "TaskGroup.h"
#include "TaskManager.h"
#include "TaskTestFixture.h"
#include "TestHelpers.h"

class TstFolderExpand : public QObject {
    Q_OBJECT

private slots:
    void recursive_folder_copy_mirrors_structure();
    void mixed_file_and_folder_selection_lands_under_dest();
};

// ---------------------------------------------------------------------------

void TstFolderExpand::recursive_folder_copy_mirrors_structure() {
    TaskTestFixture f;
    // src/{a.bin, sub/{b.bin, c.bin}, empty/}
    const QString src = f.path("src");
    const QString destBase = f.path("dest");
    QDir().mkpath(src + "/sub");
    QDir().mkpath(src + "/empty");
    QDir().mkpath(destBase);
    TestHelpers::writeBytes(src + "/a.bin", 1024);
    TestHelpers::writeBytes(src + "/sub/b.bin", 1024);
    TestHelpers::writeBytes(src + "/sub/c.bin", 1024);

    const auto pairs = FileOpsHelpers::expandToFiles(src, destBase);
    QCOMPARE(pairs.size(), 3);

    auto* group = new TaskGroup(QStringLiteral("Recursive copy"));
    for (const auto& p : pairs) {
        group->addTask(new CopyFileTask(p.src, p.dst, group));
    }
    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished(10000));

    // Every leaf file is byte-equal at the mirrored path under dest/src/.
    QVERIFY(TestHelpers::filesEqual(src + "/a.bin", destBase + "/src/a.bin"));
    QVERIFY(TestHelpers::filesEqual(src + "/sub/b.bin", destBase + "/src/sub/b.bin"));
    QVERIFY(TestHelpers::filesEqual(src + "/sub/c.bin", destBase + "/src/sub/c.bin"));
    // Empty subfolder mirrored too — survives the pair-only copy.
    QVERIFY(QFileInfo(destBase + "/src/empty").isDir());
}

void TstFolderExpand::mixed_file_and_folder_selection_lands_under_dest() {
    // Caller passes [loose-file, folder] — each is expanded independently
    // and the results all land under the same destBase. The loose file
    // gets copied at the top level; the folder gets mirrored.
    TaskTestFixture f;
    const QString destBase = f.path("dest");
    QDir().mkpath(f.path("looseFolder"));
    QDir().mkpath(destBase);
    TestHelpers::writeBytes(f.path("loose.bin"), 256);
    TestHelpers::writeBytes(f.path("looseFolder/inner.bin"), 256);

    QList<FileOpsHelpers::Pair> all;
    all.append(FileOpsHelpers::expandToFiles(f.path("loose.bin"), destBase));
    all.append(FileOpsHelpers::expandToFiles(f.path("looseFolder"), destBase));

    auto* group = new TaskGroup(QStringLiteral("Mixed copy"));
    for (const auto& p : all) {
        group->addTask(new CopyFileTask(p.src, p.dst, group));
    }
    f.mgr.enqueueGroup(group);
    QVERIFY(f.waitForGroupFinished(10000));

    QVERIFY(TestHelpers::filesEqual(f.path("loose.bin"),
                                    destBase + "/loose.bin"));
    QVERIFY(TestHelpers::filesEqual(f.path("looseFolder/inner.bin"),
                                    destBase + "/looseFolder/inner.bin"));
}

QTEST_GUILESS_MAIN(TstFolderExpand)
#include "tst_FolderExpand.moc"
