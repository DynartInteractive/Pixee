#include <QtTest>

#include "NameFilter.h"

// Coverage for the Ctrl + F name filter's matching rule: substring vs
// wildcard mode, case folding, and what an empty pattern means. The proxy
// model that applies it per row, and the bar that types into it, are UI and
// are checked by hand.
class tst_NameFilter : public QObject
{
    Q_OBJECT

private slots:
    // ---- empty means "no filter" ----
    void emptyPatternMatchesEverything() {
        QVERIFY(NameFilter().isEmpty());
        QVERIFY(NameFilter("").isEmpty());
        QVERIFY(NameFilter("   ").isEmpty());
        QVERIFY(NameFilter::matches("anything.jpg", ""));
        QVERIFY(NameFilter::matches("", "  "));
    }

    // ---- substring mode ----
    void substringAnywhereInName() {
        QVERIFY(NameFilter::matches("IMG_2024_001.jpg", "2024"));
        QVERIFY(NameFilter::matches("Trip 2024", "2024"));
        QVERIFY(NameFilter::matches("2024", "2024"));
        QVERIFY(!NameFilter::matches("IMG_2023_001.jpg", "2024"));
    }

    void substringIsCaseInsensitive() {
        QVERIFY(NameFilter::matches("Holiday.JPG", "jpg"));
        QVERIFY(NameFilter::matches("holiday.jpg", "HOLIDAY"));
        QVERIFY(NameFilter::matches(QString::fromUtf8("Árvíztűrő.png"),
                                    QString::fromUtf8("árvíz")));
    }

    void substringTrimsThePattern() {
        QVERIFY(NameFilter::matches("cat.png", " cat "));
        QCOMPARE(NameFilter(" cat ").pattern(), QString(" cat "));
    }

    void substringTreatsDotsLiterally() {
        // No regex semantics in substring mode: "." is a dot, not "any".
        QVERIFY(NameFilter::matches("a.png", ".png"));
        QVERIFY(!NameFilter::matches("apng", ".png"));
    }

    // ---- wildcard mode ----
    void hasWildcards() {
        QVERIFY(NameFilter::hasWildcards("*.jpg"));
        QVERIFY(NameFilter::hasWildcards("IMG_?"));
        QVERIFY(!NameFilter::hasWildcards("plain"));
        QVERIFY(!NameFilter::hasWildcards(""));
    }

    void starMatchesWholeName() {
        QVERIFY(NameFilter::matches("photo.jpg", "*.jpg"));
        QVERIFY(NameFilter::matches("photo.JPG", "*.jpg"));
        QVERIFY(!NameFilter::matches("photo.jpeg", "*.jpg"));
        QVERIFY(!NameFilter::matches("photo.jpg.bak", "*.jpg"));
        QVERIFY(NameFilter::matches("IMG_2024_001.jpg", "IMG_*"));
        // Anchored: a wildcard pattern is not a substring search.
        QVERIFY(!NameFilter::matches("my IMG_1.jpg", "IMG_*"));
    }

    void questionMarkMatchesOneCharacter() {
        QVERIFY(NameFilter::matches("IMG_1.jpg", "IMG_?.jpg"));
        QVERIFY(!NameFilter::matches("IMG_12.jpg", "IMG_?.jpg"));
        QVERIFY(NameFilter::matches("IMG_12.jpg", "IMG_??.*"));
    }

    void wildcardDotIsLiteral() {
        QVERIFY(!NameFilter::matches("axpng", "a.png*"));
        QVERIFY(NameFilter::matches("a.png", "a.png*"));
    }

    void wildcardMatchesFolderNamesToo() {
        QVERIFY(NameFilter::matches("2024-07 Trip", "2024*"));
        QVERIFY(!NameFilter::matches("Trip 2024", "2024*"));
    }

    // ---- the reusable matcher ----
    void matcherIsReusable() {
        const NameFilter f("*.png");
        QVERIFY(!f.isEmpty());
        QVERIFY(f.matches("a.png"));
        QVERIFY(f.matches("B.PNG"));
        QVERIFY(!f.matches("a.jpg"));
    }
};

QTEST_APPLESS_MAIN(tst_NameFilter)
#include "tst_NameFilter.moc"
