#include "PathCompleter.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStringListModel>

#include "FolderSuggester.h"

PathCompleter::PathCompleter(QLineEdit* edit, bool useBackslash, QObject* parent)
    : QObject(parent), _edit(edit), _useBackslash(useBackslash) {
    _model = new QStringListModel(this);

    _completer = new QCompleter(_model, this);
    // We filter by the last path segment ourselves (see the header) — Qt's
    // prefix match is against the whole string, which never matches a path
    // the user is midway through typing.
    _completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    _completer->setCaseSensitivity(Qt::CaseInsensitive);
    _completer->setMaxVisibleItems(12);
    // setWidget, NOT QLineEdit::setCompleter. setCompleter additionally wires
    // Qt's own "pop up on every textEdited" behaviour, which re-shows the
    // popup with the previous directory's entries immediately after we decide
    // there is nothing to show -- and makes the line edit rewrite itself to
    // the highlighted row while the user is still typing. setWidget keeps the
    // popup, the arrow keys and Esc, and leaves us in charge of when it opens
    // and what lands in the line edit.
    _completer->setWidget(_edit);

    // popup() creates the view on first call, by which point QCompleter has
    // installed its own filter on it. Ours goes on after, and event filters
    // run last-installed-first, so we get Return before QCompleter does.
    _completer->popup()->installEventFilter(this);

    _suggester = new FolderSuggester(&_abortVersion);
    _suggester->moveToThread(&_thread);
    connect(&_thread, &QThread::finished, _suggester, &QObject::deleteLater);
    connect(this, &PathCompleter::suggestRequested,
            _suggester, &FolderSuggester::suggest);
    connect(_suggester, &FolderSuggester::suggested,
            this, &PathCompleter::onSuggested);
    _thread.start();

    connect(_edit, &QLineEdit::textEdited, this, &PathCompleter::onTextEdited);
}

PathCompleter::~PathCompleter() {
    // Bump first so a listing already in flight bails instead of finishing.
    _abortVersion.fetchAndAddRelease(1);
    _thread.quit();
    _thread.wait();
}

QChar PathCompleter::separator() const {
    return _useBackslash ? QLatin1Char('\\') : QLatin1Char('/');
}

QString PathCompleter::toDisplay(const QString& path) const {
    if (!_useBackslash) return path;
    QString out = path;
    out.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return out;
}

QAbstractItemView* PathCompleter::popup() const {
    return _completer->popup();
}

void PathCompleter::hidePopup() {
    _completer->popup()->hide();
}

bool PathCompleter::split(const QString& text,
                          QString* dirPath, QString* prefix) const {
    // '/' is a separator everywhere. '\' only where it is one — on Linux it
    // is a perfectly legal character in a file name.
    int cut = text.lastIndexOf(QLatin1Char('/'));
    if (_useBackslash) {
        cut = qMax(cut, text.lastIndexOf(QLatin1Char('\\')));
    }
    if (cut < 0) return false;   // no separator: nothing to list against

    *dirPath = text.left(cut + 1);   // keeps the separator, so "Z:\" stays valid
    *prefix = text.mid(cut + 1);
    return true;
}

void PathCompleter::onTextEdited(const QString& text) {
    QString dirPath, prefix;
    if (!split(text, &dirPath, &prefix)) {
        hidePopup();
        return;
    }
    // Same directory as the last listing: re-filter what we already have.
    // This is the common case — it is what typing out one folder name does —
    // and it keeps the popup instant on a slow share.
    if (dirPath == _cachedDir) {
        show(dirPath, FolderSuggester::filterAndSort(_cachedNames, prefix));
        return;
    }
    request(dirPath, prefix);
}

void PathCompleter::request(const QString& dirPath, const QString& prefix) {
    const int version = _abortVersion.fetchAndAddOrdered(1) + 1;
    emit suggestRequested(dirPath, prefix, version);
}

void PathCompleter::onSuggested(QString dirPath, QString prefix,
                                QStringList names, int taskVersion) {
    if (_abortVersion.loadAcquire() != taskVersion) return;   // superseded

    // Cache the *filtered* list against this directory only when it is the
    // whole directory; a prefixed reply is a subset and would poison the
    // cache for a shorter prefix typed next (a backspace).
    if (prefix.isEmpty()) {
        _cachedDir = dirPath;
        _cachedNames = names;
    }
    show(dirPath, names);
}

void PathCompleter::show(const QString& dirPath, const QStringList& names) {
    if (names.isEmpty()) {
        hidePopup();
        return;
    }
    QStringList paths;
    paths.reserve(names.size());
    for (const QString& name : names) {
        paths.append(toDisplay(dirPath + name));
    }
    _model->setStringList(paths);
    _completer->complete();
    // Resetting the model drops the popup's current row, and complete() does
    // not restore it, so Enter would have nothing to act on until the user
    // arrowed down. Select the first row explicitly. Safe here precisely
    // because we used setWidget above: this does not write into the line edit.
    _completer->popup()->setCurrentIndex(_completer->completionModel()->index(0, 0));
}

void PathCompleter::acceptCurrent() {
    QModelIndex idx = _completer->popup()->currentIndex();
    if (!idx.isValid()) {
        // Nothing highlighted (the user never arrowed): act on the first row,
        // which is what the highlight shows anyway.
        idx = _completer->completionModel()->index(0, 0);
    }
    if (!idx.isValid()) {
        hidePopup();
        return;
    }
    QString chosen = idx.data(Qt::DisplayRole).toString();
    // Leave a trailing separator so the next listing is of the folder just
    // chosen, not a re-filter of its siblings — that is what makes repeated
    // Enter walk down the tree.
    if (!chosen.endsWith(separator())) chosen.append(separator());
    _edit->setText(chosen);

    // Ask for the children. An empty reply hides the popup, so Enter on a
    // leaf folder leaves the path set and the way clear for the next Enter
    // to navigate there.
    request(chosen, QString());
}

bool PathCompleter::eventFilter(QObject* watched, QEvent* event) {
    if (watched != _completer->popup() || event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }
    const int key = static_cast<QKeyEvent*>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return QObject::eventFilter(watched, event);   // Esc/arrows: Qt's job
    }
    // Consume it. Passing it on would let QCompleter re-emit it at the line
    // edit as returnPressed, which navigates — see the header.
    acceptCurrent();
    return true;
}
