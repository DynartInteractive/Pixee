#ifndef FOLDERSUGGESTER_H
#define FOLDERSUGGESTER_H

#include <QAtomicInt>
#include <QObject>
#include <QString>
#include <QStringList>

// Lists a directory's sub-folders off the GUI thread, for the path bar's
// completion popup (PathCompleter).
//
// The read is off-thread for the reason the whole app exists: a directory
// listing on an SMB share can take long enough to notice, and a completion
// popup reads a directory *while the user is typing into the widget it hangs
// off*. A synchronous QDir::entryList there would stutter the very keystrokes
// it is meant to help.
//
// Same supersede-on-change pattern as PreviewReader / MetadataReader: a
// request carries a taskVersion snapshot and is dropped once the live counter
// has moved on, so walking a deep path never builds a backlog of listings
// nobody is waiting for any more.
class FolderSuggester : public QObject
{
    Q_OBJECT
public:
    // Cap on one reply. The popup shows a handful of rows and the user
    // narrows by typing, so handing over a 5000-entry folder in full would
    // cost a large model and a long sort to display ten of them.
    static constexpr int kMaxSuggestions = 200;

    explicit FolderSuggester(QAtomicInt* abortVersion, QObject* parent = nullptr);

    // Prefix-filter (case-insensitive) and order a raw name list. Shared with
    // PathCompleter, which re-filters its cached listing locally while the
    // user types within one directory rather than re-reading it per keystroke
    // — so both paths must agree on what the popup shows.
    static QStringList filterAndSort(const QStringList& names,
                                     const QString& prefix);

public slots:
    void suggest(QString dirPath, QString prefix, int taskVersion);

signals:
    void suggested(QString dirPath, QString prefix, QStringList names,
                   int taskVersion);

private:
    bool isAborted(int taskVersion) const;
    QAtomicInt* _abortVersion;
};

#endif // FOLDERSUGGESTER_H
