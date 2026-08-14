#ifndef BATCHRENAMEPLAN_H
#define BATCHRENAMEPLAN_H

#include <QList>
#include <QPair>
#include <QString>

// Pure (GUI-free, filesystem-free) name-computation and ordering logic behind
// the batch-rename dialog. Kept separate from the widget so it can be unit
// tested with QtCore alone.
namespace BatchRename {

// The transform applied to every selected file, in this fixed order:
//   1. find & replace on the (extension-stripped, if keepExtension) base name
//   2. substitute into `pattern`: {name} → the result of step 1,
//      {n} / {n:<width>} → a per-file counter (startNumber + index*step,
//      zero-padded to <width>)
//   3. re-attach the original extension when keepExtension is set
// Prefix / suffix are expressed through the pattern, e.g. "IMG_{name}" or
// "{name}_edited"; plain "{name}" is a no-op template.
struct Options {
    QString findText;
    QString replaceText;
    bool    caseSensitive = false;
    QString pattern = QStringLiteral("{name}");
    int     startNumber = 1;
    int     step = 1;
    bool    keepExtension = true;
};

// New file name (name only, no directory) for one source file at 0-based
// `index` in the batch. Deterministic; does not touch the filesystem.
QString newNameFor(const QString& fileName, const Options& opts, int index);

// One concrete rename step: absolute `from` path → absolute `to` path.
struct Step {
    QString from;
    QString to;
};

// Result of ordering a set of final renames into a clobber-safe sequence.
struct Plan {
    QList<Step> steps;      // execute in this order
    bool hasCycle = false;  // true → a swap cycle (a↔b) that plain renames
                            // can't resolve; steps is empty in that case
};

// Order `renames` (absolute old→new pairs, no-ops allowed) so that no rename
// ever overwrites a file that is still waiting to be renamed. Chains like
// a→b→c are handled by scheduling the tail first. A true cycle (a→b, b→a) has
// no safe plain-rename order → hasCycle is set and the caller should refuse.
Plan planRenameSteps(const QList<QPair<QString, QString>>& renames);

}  // namespace BatchRename

#endif // BATCHRENAMEPLAN_H
