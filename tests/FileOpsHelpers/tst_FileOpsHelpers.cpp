#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "FileOpsHelpers.h"
#include "TestHelpers.h"

class TstFileOpsHelpers : public QObject {
    Q_OBJECT

private slots:
    // Phase 1 smoke: framework + temp-dir + writeBytes pipeline all work.
    // Real assertions for the helpers land in Phase 2.
    void harnessSmoke() {
        QTemporaryDir tmp;
        QVERIFY2(tmp.isValid(), "QTemporaryDir failed to create a directory");
        const QString p = tmp.path() + "/probe.bin";
        TestHelpers::writeBytes(p, 1024);
        QVERIFY(QFile::exists(p));
        QCOMPARE(QFileInfo(p).size(), qint64(1024));
    }
};

QTEST_GUILESS_MAIN(TstFileOpsHelpers)
#include "tst_FileOpsHelpers.moc"
