#include <QtTest>

#include "FolderSuggester.h"

// Pure coverage for the path bar's suggestion filtering: which sub-folder
// names survive a prefix, in what order, and how many. The directory read
// itself is Qt's, and the popup wiring needs a widget, so neither is here.
class tst_FolderSuggester : public QObject
{
    Q_OBJECT

    static QStringList sample() {
        // Deliberately unsorted, and mixed case.
        return {"tura2", "Camera", "tura", "album", "Zebra", "Album2"};
    }

private slots:
    // ---- no prefix: everything, ordered ----
    void emptyPrefixKeepsAllAndSorts() {
        QCOMPARE(FolderSuggester::filterAndSort(sample(), QString()),
                 QStringList({"album", "Album2", "Camera", "tura", "tura2", "Zebra"}));
    }

    void emptyInputStaysEmpty() {
        QCOMPARE(FolderSuggester::filterAndSort({}, QString()), QStringList());
        QCOMPARE(FolderSuggester::filterAndSort({}, "x"), QStringList());
    }

    // ---- prefix matching ----
    void prefixFilters() {
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "tur"),
                 QStringList({"tura", "tura2"}));
    }

    // Typing 'c' should find 'Camera'. A case-sensitive match here would make
    // the popup feel broken on Windows, where the path itself is caseless.
    void prefixIsCaseInsensitive() {
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "c"),
                 QStringList({"Camera"}));
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "AL"),
                 QStringList({"album", "Album2"}));
    }

    void prefixMatchesStartOnly() {
        // 'era' is inside 'Camera' but not at the front.
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "era"), QStringList());
    }

    void wholeNameMatchesItself() {
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "tura"),
                 QStringList({"tura", "tura2"}));
    }

    void noMatchGivesEmpty() {
        QCOMPARE(FolderSuggester::filterAndSort(sample(), "zzz"), QStringList());
    }

    // ---- ordering matches the browser ----
    // FileModel::nameLessThan compares case-insensitively first, so names
    // that differ only in case stay adjacent instead of splitting into two
    // ASCII blocks ('Zebra' before 'album' would be the plain-< result).
    // Ties then fall back to plain <, which puts uppercase first.
    void orderIsCaseInsensitiveNotAscii() {
        QCOMPARE(FolderSuggester::filterAndSort({"beta", "Alpha", "alpha"}, QString()),
                 QStringList({"Alpha", "alpha", "beta"}));
        QCOMPARE(FolderSuggester::filterAndSort({"Zebra", "album"}, QString()),
                 QStringList({"album", "Zebra"}));
    }

    // ---- the cap ----
    void capsLongListsButKeepsTheEarliest() {
        QStringList many;
        for (int i = 0; i < FolderSuggester::kMaxSuggestions + 50; ++i) {
            many << QStringLiteral("f%1").arg(i, 5, 10, QLatin1Char('0'));
        }
        const QStringList out = FolderSuggester::filterAndSort(many, QString());
        QCOMPARE(out.size(), FolderSuggester::kMaxSuggestions);
        // Capped after sorting, so it is the first N in order, not an
        // arbitrary N that then look shuffled.
        QCOMPARE(out.first(), QStringLiteral("f00000"));
        QCOMPARE(out.last(),
                 QStringLiteral("f%1").arg(FolderSuggester::kMaxSuggestions - 1,
                                           5, 10, QLatin1Char('0')));
    }
};

QTEST_MAIN(tst_FolderSuggester)
#include "tst_FolderSuggester.moc"
