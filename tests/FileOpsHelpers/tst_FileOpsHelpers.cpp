#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "FileOpsHelpers.h"
#include "TestHelpers.h"

using FileOpsHelpers::clipboardSaysCut;
using FileOpsHelpers::destIsSourceOrDescendant;
using FileOpsHelpers::dropEffectBytes;
using FileOpsHelpers::expandToFiles;
using FileOpsHelpers::isDriveRoot;
using FileOpsHelpers::kDropEffectMime;
using FileOpsHelpers::Pair;
using FileOpsHelpers::uniqueRenamedPath;

class TstFileOpsHelpers : public QObject {
    Q_OBJECT

private slots:
    void harnessSmoke();

    void isDriveRoot_recognises_drive_and_posix_roots();
    void isDriveRoot_rejects_ordinary_paths();

    void destIsSourceOrDescendant_identical_paths();
    void destIsSourceOrDescendant_descendants();
    void destIsSourceOrDescendant_unrelated_paths();
    void destIsSourceOrDescendant_substring_trap();
    void destIsSourceOrDescendant_case_sensitivity();

    void expandToFiles_file_source();
    void expandToFiles_empty_folder();
    void expandToFiles_nested_folder();
    void expandToFiles_missing_source();

    void uniqueRenamedPath_returns_input_when_absent();
    void uniqueRenamedPath_appends_one_on_first_collision();
    void uniqueRenamedPath_increments_until_free();
    void uniqueRenamedPath_no_extension();

    void dropEffectBytes_roundtrip();
    void clipboardSaysCut_null_mime();
    void clipboardSaysCut_no_drop_effect_format();
    void clipboardSaysCut_short_payload_is_safe();
    void clipboardSaysCut_recognises_move_only();
};

// ---------------------------------------------------------------------------
// harness
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::harnessSmoke() {
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "QTemporaryDir failed to create a directory");
    const QString p = tmp.path() + "/probe.bin";
    TestHelpers::writeBytes(p, 1024);
    QVERIFY(QFile::exists(p));
    QCOMPARE(QFileInfo(p).size(), qint64(1024));
}

// ---------------------------------------------------------------------------
// isDriveRoot
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::isDriveRoot_recognises_drive_and_posix_roots() {
#ifdef Q_OS_WIN
    QVERIFY(isDriveRoot("C:/"));
    QVERIFY(isDriveRoot("C:\\"));
    QVERIFY(isDriveRoot("D:/"));
#else
    QVERIFY(isDriveRoot("/"));
#endif
}

void TstFileOpsHelpers::isDriveRoot_rejects_ordinary_paths() {
    QVERIFY(!isDriveRoot("C:/foo"));
    QVERIFY(!isDriveRoot("C:/foo/bar.txt"));
    QVERIFY(!isDriveRoot("/home/gopher"));
    QVERIFY(!isDriveRoot(""));
}

// ---------------------------------------------------------------------------
// destIsSourceOrDescendant
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::destIsSourceOrDescendant_identical_paths() {
    QVERIFY(destIsSourceOrDescendant("C:/foo", "C:/foo"));
    QVERIFY(destIsSourceOrDescendant("C:/foo/", "C:/foo"));
    QVERIFY(destIsSourceOrDescendant("C:/foo/.", "C:/foo"));
    // Mixed separators normalise via cleanPath().
    QVERIFY(destIsSourceOrDescendant("C:\\foo", "C:/foo"));
}

void TstFileOpsHelpers::destIsSourceOrDescendant_descendants() {
    QVERIFY(destIsSourceOrDescendant("C:/foo/bar", "C:/foo"));
    QVERIFY(destIsSourceOrDescendant("C:/foo/bar/baz", "C:/foo"));
    QVERIFY(destIsSourceOrDescendant("C:/foo/bar/baz/", "C:/foo"));
}

void TstFileOpsHelpers::destIsSourceOrDescendant_unrelated_paths() {
    QVERIFY(!destIsSourceOrDescendant("C:/baz", "C:/foo"));
    QVERIFY(!destIsSourceOrDescendant("C:/", "C:/foo"));         // parent isn't a descendant
    QVERIFY(!destIsSourceOrDescendant("D:/foo/bar", "C:/foo"));   // different drive
}

void TstFileOpsHelpers::destIsSourceOrDescendant_substring_trap() {
    // 'C:/foobar' must not match 'C:/foo' just because of the prefix;
    // the boundary check uses '/' as separator, not raw startsWith.
    QVERIFY(!destIsSourceOrDescendant("C:/foobar", "C:/foo"));
    QVERIFY(!destIsSourceOrDescendant("C:/foo_bar", "C:/foo"));
}

void TstFileOpsHelpers::destIsSourceOrDescendant_case_sensitivity() {
    // Documented gap: comparison is case-sensitive even on Windows where
    // the filesystem itself is case-insensitive. Casing-only difference
    // returns false. If this surfaces as a real bug, switch to
    // QString::compare(...,Qt::CaseInsensitive) on Windows.
    QVERIFY(!destIsSourceOrDescendant("C:/Foo/bar", "C:/foo"));
}

// ---------------------------------------------------------------------------
// expandToFiles
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::expandToFiles_file_source() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString src = tmp.path() + "/a.bin";
    const QString destBase = tmp.path() + "/dest";
    QDir().mkpath(destBase);
    TestHelpers::writeBytes(src, 16);

    const QList<Pair> pairs = expandToFiles(src, destBase);
    QCOMPARE(pairs.size(), 1);
    QCOMPARE(pairs.first().src, src);
    QCOMPARE(QDir::cleanPath(pairs.first().dst),
             QDir::cleanPath(destBase + "/a.bin"));
}

void TstFileOpsHelpers::expandToFiles_empty_folder() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString src = tmp.path() + "/srcdir";
    const QString destBase = tmp.path() + "/dest";
    QDir().mkpath(src);
    QDir().mkpath(destBase);

    const QList<Pair> pairs = expandToFiles(src, destBase);
    QCOMPARE(pairs.size(), 0);
    // The destination folder mirror gets created even when there's nothing
    // to copy — empty subfolders survive the operation.
    QVERIFY(QFileInfo(destBase + "/srcdir").isDir());
}

void TstFileOpsHelpers::expandToFiles_nested_folder() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString src = tmp.path() + "/src";
    const QString destBase = tmp.path() + "/dest";
    QDir().mkpath(src + "/sub");
    QDir().mkpath(src + "/empty");
    QDir().mkpath(destBase);
    TestHelpers::writeBytes(src + "/a.bin", 8);
    TestHelpers::writeBytes(src + "/sub/b.bin", 8);
    TestHelpers::writeBytes(src + "/sub/c.bin", 8);

    QList<Pair> pairs = expandToFiles(src, destBase);
    // Iteration order of QDirIterator is not guaranteed across platforms;
    // sort by destination path before comparing.
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair& a, const Pair& b) { return a.dst < b.dst; });

    QCOMPARE(pairs.size(), 3);
    auto destOf = [&](const QString& rel) {
        return QDir::cleanPath(destBase + "/src/" + rel);
    };
    QCOMPARE(QDir::cleanPath(pairs[0].dst), destOf("a.bin"));
    QCOMPARE(QDir::cleanPath(pairs[1].dst), destOf("sub/b.bin"));
    QCOMPARE(QDir::cleanPath(pairs[2].dst), destOf("sub/c.bin"));
    // Empty subfolder mirrored to dest.
    QVERIFY(QFileInfo(destBase + "/src/empty").isDir());
}

void TstFileOpsHelpers::expandToFiles_missing_source() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString src = tmp.path() + "/does-not-exist";
    const QString destBase = tmp.path() + "/dest";
    QDir().mkpath(destBase);

    const QList<Pair> pairs = expandToFiles(src, destBase);
    QCOMPARE(pairs.size(), 0);
}

// ---------------------------------------------------------------------------
// uniqueRenamedPath
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::uniqueRenamedPath_returns_input_when_absent() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString p = tmp.path() + "/free.txt";
    QCOMPARE(uniqueRenamedPath(p), p);
}

void TstFileOpsHelpers::uniqueRenamedPath_appends_one_on_first_collision() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString p = tmp.path() + "/foo.txt";
    TestHelpers::writeBytes(p, 4);

    const QString out = uniqueRenamedPath(p);
    QCOMPARE(QDir::cleanPath(out),
             QDir::cleanPath(tmp.path() + "/foo (1).txt"));
}

void TstFileOpsHelpers::uniqueRenamedPath_increments_until_free() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    TestHelpers::writeBytes(tmp.path() + "/foo.txt", 4);
    TestHelpers::writeBytes(tmp.path() + "/foo (1).txt", 4);
    TestHelpers::writeBytes(tmp.path() + "/foo (2).txt", 4);

    const QString out = uniqueRenamedPath(tmp.path() + "/foo.txt");
    QCOMPARE(QDir::cleanPath(out),
             QDir::cleanPath(tmp.path() + "/foo (3).txt"));
}

void TstFileOpsHelpers::uniqueRenamedPath_no_extension() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString p = tmp.path() + "/README";
    TestHelpers::writeBytes(p, 4);

    const QString out = uniqueRenamedPath(p);
    QCOMPARE(QDir::cleanPath(out),
             QDir::cleanPath(tmp.path() + "/README (1)"));
}

// ---------------------------------------------------------------------------
// dropEffectBytes / clipboardSaysCut
// ---------------------------------------------------------------------------

void TstFileOpsHelpers::dropEffectBytes_roundtrip() {
    // Each well-known DROPEFFECT, encoded then decoded via a QMimeData
    // payload, must read back as the original value (so clipboardSaysCut
    // is a faithful inverse for the only effect we care about — MOVE).
    const quint32 effects[] = { 1, 2, 4, 5 };
    for (quint32 effect : effects) {
        QMimeData mime;
        mime.setData(kDropEffectMime, dropEffectBytes(effect));
        QCOMPARE(clipboardSaysCut(&mime), effect == 2);
    }
}

void TstFileOpsHelpers::clipboardSaysCut_null_mime() {
    QCOMPARE(clipboardSaysCut(nullptr), false);
}

void TstFileOpsHelpers::clipboardSaysCut_no_drop_effect_format() {
    QMimeData mime;
    mime.setText("hello");
    QCOMPARE(clipboardSaysCut(&mime), false);
}

void TstFileOpsHelpers::clipboardSaysCut_short_payload_is_safe() {
    // A truncated 3-byte payload would underflow our static_cast<quint8>
    // bit-shift if the length check were missing; assert it returns false
    // without crashing.
    QMimeData mime;
    mime.setData(kDropEffectMime, QByteArray("\x02\x00\x00", 3));
    QCOMPARE(clipboardSaysCut(&mime), false);
}

void TstFileOpsHelpers::clipboardSaysCut_recognises_move_only() {
    QMimeData moveMime;
    moveMime.setData(kDropEffectMime, dropEffectBytes(2));
    QVERIFY(clipboardSaysCut(&moveMime));

    QMimeData copyMime;
    copyMime.setData(kDropEffectMime, dropEffectBytes(5));
    QVERIFY(!clipboardSaysCut(&copyMime));
}

QTEST_GUILESS_MAIN(TstFileOpsHelpers)
#include "tst_FileOpsHelpers.moc"
