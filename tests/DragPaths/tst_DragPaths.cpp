// DragPaths: what a Pixee drag carries and how a drop reads it back — the
// private path list (Windows) or URLs (elsewhere), URLs from other apps,
// and the junk a drop must ignore.

#include <QMimeData>
#include <QUrl>
#include <QtTest>

#include "DragPaths.h"
#include "FileOpsHelpers.h"

class TstDragPaths : public QObject {
    Q_OBJECT

private slots:
    void drag_round_trips_its_paths();
    void drag_carries_copy_drop_effect();
    void empty_list_makes_nothing();
    void urls_from_other_apps_are_read();
    void remote_urls_and_text_are_ignored();
    void private_type_wins_over_urls();
};

void TstDragPaths::drag_round_trips_its_paths() {
    const QStringList paths{ "C:/Photos/a b.jpg", "C:/Photos/sub/c.png" };
    QScopedPointer<QMimeData> mime(DragPaths::makeDragMimeData(paths));
    QVERIFY(mime);
    QVERIFY(DragPaths::hasLocalPaths(mime.data()));
    QCOMPARE(DragPaths::localPaths(mime.data()), paths);
#ifdef Q_OS_WIN
    // The whole point on Windows: no URLs, so Qt exports no URL formats.
    QVERIFY(!mime->hasUrls());
    QVERIFY(!mime->hasText());
    QVERIFY(mime->hasFormat(QLatin1String(DragPaths::kMime)));
#else
    QVERIFY(mime->hasUrls());
#endif
}

void TstDragPaths::drag_carries_copy_drop_effect() {
    QScopedPointer<QMimeData> mime(DragPaths::makeDragMimeData({ "C:/x.jpg" }));
    QVERIFY(mime);
    QVERIFY(mime->hasFormat(QLatin1String(FileOpsHelpers::kDropEffectMime)));
    QVERIFY(!FileOpsHelpers::clipboardSaysCut(mime.data()));
}

void TstDragPaths::empty_list_makes_nothing() {
    QVERIFY(DragPaths::makeDragMimeData({}) == nullptr);
    QVERIFY(!DragPaths::hasLocalPaths(nullptr));
    QVERIFY(DragPaths::localPaths(nullptr).isEmpty());
}

void TstDragPaths::urls_from_other_apps_are_read() {
    QMimeData mime;
    mime.setUrls({ QUrl::fromLocalFile("C:/Photos/a.jpg"), QUrl::fromLocalFile("C:/Photos/b.jpg") });
    QCOMPARE(DragPaths::localPaths(&mime), (QStringList{ "C:/Photos/a.jpg", "C:/Photos/b.jpg" }));
}

void TstDragPaths::remote_urls_and_text_are_ignored() {
    QMimeData mime;
    mime.setUrls({ QUrl("https://example.com/a.jpg") });
    mime.setText("C:/Photos/a.jpg");
    QVERIFY(!DragPaths::hasLocalPaths(&mime));
}

void TstDragPaths::private_type_wins_over_urls() {
    QMimeData mime;
    mime.setData(QLatin1String(DragPaths::kMime), QByteArray("C:/one.jpg\nC:/two.jpg"));
    mime.setUrls({ QUrl::fromLocalFile("C:/other.jpg") });
    QCOMPARE(DragPaths::localPaths(&mime), (QStringList{ "C:/one.jpg", "C:/two.jpg" }));
}

QTEST_MAIN(TstDragPaths)
#include "tst_DragPaths.moc"
