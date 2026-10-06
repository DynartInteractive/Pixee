#include "BatchRenamePlan.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSet>

namespace {
// Paths compare per the filesystem's case rule: on Windows / macOS "A.jpg"
// and "a.jpg" are the same file, so a rename onto one clobbers the other.
// (Kept local rather than shared so this unit stays QtCore-only.)
QString pathKey(const QString& path) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return path.toLower();
#else
    return path;
#endif
}

// {n:<width>} beyond this is a typo, not a request for a gigabyte of zeros.
constexpr int kMaxNumberWidth = 32;
}  // namespace

namespace BatchRename {

QString newNameFor(const QString& fileName, const Options& opts, int index) {
    // Split base / extension. dot > 0 so a dotfile (".bashrc") keeps its whole
    // name as the base rather than being treated as all-extension.
    QString base = fileName;
    QString ext;
    if (opts.keepExtension) {
        const int dot = fileName.lastIndexOf(QLatin1Char('.'));
        if (dot > 0) {
            base = fileName.left(dot);
            ext = fileName.mid(dot);  // includes the leading dot
        }
    }

    // 1) find & replace on the base
    if (!opts.findText.isEmpty()) {
        base.replace(opts.findText, opts.replaceText,
                     opts.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    // 2) pattern substitution: every {n} / {n:<width>} in the *pattern* first,
    //    then {name}. In that order a literal "{n}" inside a file name (or
    //    the replace text) stays literal instead of being numbered.
    const QString& pattern = opts.pattern;

    const long long number =
        static_cast<long long>(opts.startNumber) + static_cast<long long>(index) * opts.step;
    static const QRegularExpression numRe(QStringLiteral("\\{n(?::(\\d+))?\\}"));
    QString rebuilt;
    int last = 0;
    QRegularExpressionMatchIterator it = numRe.globalMatch(pattern);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        rebuilt += pattern.mid(last, m.capturedStart() - last);
        const int width = m.captured(1).isEmpty()
            ? 0 : qMin(m.captured(1).toInt(), kMaxNumberWidth);
        QString num = QString::number(number);
        if (width > 0)
            num = QStringLiteral("%1").arg(number, width, 10, QLatin1Char('0'));
        rebuilt += num;
        last = m.capturedEnd();
    }
    rebuilt += pattern.mid(last);
    rebuilt.replace(QStringLiteral("{name}"), base);

    return rebuilt + ext;
}

Plan planRenameSteps(const QList<QPair<QString, QString>>& renames) {
    Plan plan;

    // Pending set of source paths still to be renamed. A rename old→new is
    // safe to schedule now iff `new` is not the source of any *other* still-
    // pending rename (renaming onto it would clobber a file we still need).
    QList<QPair<QString, QString>> pending;
    QSet<QString> pendingSources;
    for (const auto& r : renames) {
        if (r.first == r.second) continue;  // drop no-ops
        pending.append(r);
        pendingSources.insert(pathKey(r.first));
    }

    while (!pending.isEmpty()) {
        int pick = -1;
        for (int i = 0; i < pending.size(); ++i) {
            const QString target = pathKey(pending.at(i).second);
            // Free if the target isn't a pending source, or it's this rename's
            // own source — a case-only rename ("a.jpg" → "A.jpg") where the
            // filesystem sees one file.
            if (!pendingSources.contains(target)
                    || target == pathKey(pending.at(i).first)) {
                pick = i;
                break;
            }
        }
        if (pick < 0) {
            // Every remaining target is still an occupied source → a cycle.
            plan.hasCycle = true;
            plan.steps.clear();
            return plan;
        }
        const QPair<QString, QString> chosen = pending.takeAt(pick);
        pendingSources.remove(pathKey(chosen.first));
        plan.steps.append({ chosen.first, chosen.second });
    }

    return plan;
}

}  // namespace BatchRename
