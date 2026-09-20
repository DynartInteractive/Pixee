#ifndef PATHCOMPLETER_H
#define PATHCOMPLETER_H

#include <QAtomicInt>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

class FolderSuggester;
class QAbstractItemView;
class QCompleter;
class QLineEdit;
class QStringListModel;

// Sub-folder completion for the browser's path bar.
//
// Typing into the path bar pops up the sub-folders of the directory to the
// left of the caret, listed as full paths. Up/Down walk them, Enter descends
// into the highlighted folder and immediately offers *its* children, Esc
// closes the popup. With the popup closed, Enter still navigates, which is
// the path bar's original job. Arrowing deliberately does not rewrite the
// line edit — the text stays as typed until Enter commits a choice.
//
// QCompleter supplies the popup, the arrow keys and Esc. Two things it does
// NOT supply, both verified against Qt 6.11 rather than assumed:
//
//  - **Enter fires BOTH the widget's returnPressed AND QCompleter::activated**
//    (returnPressed first). Left alone, one Enter would navigate away *and*
//    complete. So PathCompleter event-filters the popup and consumes
//    Return/Enter itself. QCompleter installs its own filter on the popup when
//    the popup is created, and filters run last-installed-first, so installing
//    ours afterwards means QCompleter never sees the key at all. This is why
//    MainWindow needs no "is the popup up?" guard in its returnPressed slot.
//
//  - QCompleter's filtering is prefix-on-the-whole-string, which is no use
//    when the string is a path and only the last segment is being typed. The
//    mode is therefore UnfilteredPopupCompletion and the filtering happens in
//    FolderSuggester, over names rather than paths.
//
// Directory reads go to FolderSuggester on a worker thread, superseded by an
// abort-version counter. On top of that the last listing is cached by
// directory: typing within one path segment re-filters the cached names with
// no disk access at all, so a folder is read once per level rather than once
// per keystroke. That matters on the SMB shares this app is built for.
class PathCompleter : public QObject
{
    Q_OBJECT
public:
    // `useBackslash` follows Config::useBackslash() — it picks the separator
    // written back into the line edit, and whether '\' counts as a separator
    // when splitting (on Linux it is a legal filename character, not one).
    PathCompleter(QLineEdit* edit, bool useBackslash, QObject* parent = nullptr);
    ~PathCompleter() override;

    // The suggestion list. Exposed so callers can ask whether it is up, and
    // so it can be driven in a test.
    QAbstractItemView* popup() const;
    // Close it — used when something other than typing changes the path, so
    // the list can't linger over a directory the user has already left.
    void hidePopup();

signals:
    void suggestRequested(QString dirPath, QString prefix, int taskVersion);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTextEdited(const QString& text);
    void onSuggested(QString dirPath, QString prefix, QStringList names,
                     int taskVersion);

private:
    // Split at the last separator: everything up to and including it is the
    // directory to list, the rest is the prefix to match. Returns false when
    // there is no separator, i.e. nothing to anchor a listing to.
    bool split(const QString& text, QString* dirPath, QString* prefix) const;
    void request(const QString& dirPath, const QString& prefix);
    void show(const QString& dirPath, const QStringList& names);
    void acceptCurrent();
    QChar separator() const;
    QString toDisplay(const QString& path) const;

    QLineEdit* _edit;
    bool _useBackslash;
    QCompleter* _completer;
    QStringListModel* _model;

    QThread _thread;
    FolderSuggester* _suggester;
    QAtomicInt _abortVersion;

    // Last listing, keyed by the directory it came from, so typing within one
    // segment never re-reads it.
    QString _cachedDir;
    QStringList _cachedNames;
};

#endif // PATHCOMPLETER_H
