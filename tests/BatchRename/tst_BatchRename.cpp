#include <QtTest>

#include "BatchRenamePlan.h"

using BatchRename::Options;
using BatchRename::newNameFor;
using BatchRename::Plan;
using BatchRename::planRenameSteps;
using Pair = QPair<QString, QString>;

// Pure-logic coverage for the batch-rename engine: name computation
// (find/replace + {name}/{n} pattern + extension handling) and the
// clobber-safe rename ordering (chains, no-ops, cycles).
class tst_BatchRename : public QObject
{
    Q_OBJECT

private slots:
    // ---- newNameFor: pattern / prefix / suffix ----
    void patternNoop() {
        Options o;  // pattern "{name}", keepExtension
        QCOMPARE(newNameFor("photo.jpg", o, 0), QStringLiteral("photo.jpg"));
    }

    void prefixAndSuffix() {
        Options o;
        o.pattern = "IMG_{name}";
        QCOMPARE(newNameFor("photo.jpg", o, 0), QStringLiteral("IMG_photo.jpg"));
        o.pattern = "{name}_edited";
        QCOMPARE(newNameFor("photo.jpg", o, 0), QStringLiteral("photo_edited.jpg"));
    }

    // ---- newNameFor: numbering ----
    void numberingPadded() {
        Options o;
        o.pattern = "Holiday_{n:3}";
        QCOMPARE(newNameFor("a.png", o, 0), QStringLiteral("Holiday_001.png"));
        QCOMPARE(newNameFor("b.png", o, 9), QStringLiteral("Holiday_010.png"));
    }

    void numberingStartStep() {
        Options o;
        o.pattern = "x{n}";
        o.startNumber = 5;
        o.step = 2;
        QCOMPARE(newNameFor("f.txt", o, 0), QStringLiteral("x5.txt"));
        QCOMPARE(newNameFor("f.txt", o, 3), QStringLiteral("x11.txt"));  // 5 + 3*2
    }

    // ---- newNameFor: find & replace ----
    void findReplaceCaseInsensitiveByDefault() {
        Options o;
        o.findText = "img";
        o.replaceText = "Pic";
        QCOMPARE(newNameFor("IMG_1234.jpg", o, 0), QStringLiteral("Pic_1234.jpg"));
    }

    void findReplaceCaseSensitive() {
        Options o;
        o.findText = "img";
        o.replaceText = "Pic";
        o.caseSensitive = true;
        // "IMG" doesn't match lowercase "img" → base unchanged.
        QCOMPARE(newNameFor("IMG_1.jpg", o, 0), QStringLiteral("IMG_1.jpg"));
    }

    // ---- extension handling ----
    void keepExtensionOff() {
        Options o;
        o.keepExtension = false;
        o.findText = ".jpg";
        o.replaceText = "";
        // With no extension split, ".jpg" is part of the base and is stripped.
        QCOMPARE(newNameFor("a.jpg", o, 0), QStringLiteral("a"));
    }

    void dotfileKeepsWholeName() {
        Options o;  // keepExtension, pattern "{name}"
        // Leading dot is not an extension boundary → whole name is the base.
        QCOMPARE(newNameFor(".bashrc", o, 0), QStringLiteral(".bashrc"));
    }

    void findReplaceThenNumber() {
        Options o;
        o.findText = "DSC";
        o.replaceText = "Trip";
        o.pattern = "{name}_{n:2}";
        QCOMPARE(newNameFor("DSC_0001.jpg", o, 0), QStringLiteral("Trip_0001_01.jpg"));
    }

    // ---- planRenameSteps ----
    void planNoOverlap() {
        const Plan p = planRenameSteps({ { "a", "b" }, { "c", "d" } });
        QVERIFY(!p.hasCycle);
        QCOMPARE(p.steps.size(), 2);
        // both renames present (order unconstrained here)
        QVERIFY(hasStep(p, "a", "b"));
        QVERIFY(hasStep(p, "c", "d"));
    }

    void planDropsNoops() {
        const Plan p = planRenameSteps({ { "a", "a" }, { "b", "c" } });
        QVERIFY(!p.hasCycle);
        QCOMPARE(p.steps.size(), 1);
        QVERIFY(hasStep(p, "b", "c"));
    }

    void planChainOrdersTailFirst() {
        // a→b, b→c: b→c must run first, else a→b clobbers b before it moves.
        const Plan p = planRenameSteps({ { "a", "b" }, { "b", "c" } });
        QVERIFY(!p.hasCycle);
        QCOMPARE(p.steps.size(), 2);
        QCOMPARE(p.steps.at(0).from, QStringLiteral("b"));
        QCOMPARE(p.steps.at(0).to,   QStringLiteral("c"));
        QCOMPARE(p.steps.at(1).from, QStringLiteral("a"));
        QCOMPARE(p.steps.at(1).to,   QStringLiteral("b"));
    }

    void planDetectsCycle() {
        const Plan p = planRenameSteps({ { "a", "b" }, { "b", "a" } });
        QVERIFY(p.hasCycle);
        QVERIFY(p.steps.isEmpty());
    }

private:
    static bool hasStep(const Plan& p, const QString& from, const QString& to) {
        for (const auto& s : p.steps)
            if (s.from == from && s.to == to) return true;
        return false;
    }
};

QTEST_MAIN(tst_BatchRename)
#include "tst_BatchRename.moc"
