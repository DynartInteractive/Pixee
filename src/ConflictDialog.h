#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

#include "Task.h"

class QCheckBox;
class QImage;
class QLabel;
class ThumbnailCache;

// Modal conflict prompt shown when a task hits a destination that already
// exists (Task::DestinationExists). Replaces the old inline question strip
// that lived inside the task row: a task blocked on a conflict now surfaces
// a real blocking dialog instead of a set of buttons buried in the dock.
//
// Shows the two files side by side — "Existing" (the destination) vs
// "Incoming" (the source) — each with a thumbnail and a dimensions / size /
// date caption, so the choice can be made visually rather than by path alone.
// Thumbnails are loaded off-thread through the shared ThumbnailCache: instant
// when the file was just browsed, a background decode otherwise. The async
// thumbnailReady signals are delivered while exec() spins its nested event
// loop, so the previews fill in live without blocking the modal.
//
// Offers Skip / Rename / Overwrite plus an "apply to all" checkbox that maps
// to the group's sticky answer (so "Overwrite" + checkbox behaves like the
// old "Overwrite All"). The caller reads answer() / applyToAll() after
// exec() returns; Escape / close == Skip (the safe default), so the return
// code itself is not consulted.
class ConflictDialog : public QDialog {
    Q_OBJECT
public:
    // thumbs may be null (e.g. in tests) — the panes then just show the file
    // icon + caption with no live thumbnail.
    ConflictDialog(int kind, const QVariantMap& context,
                   ThumbnailCache* thumbs, QWidget* parent = nullptr);
    ~ConflictDialog() override;

    Task::ConflictAnswer answer() const { return _answer; }
    bool applyToAll() const { return _applyToAll; }

private slots:
    void onThumbnailReady(const QString& path, const QImage& image);

private:
    void chooseAndAccept(Task::ConflictAnswer answer);
    // Build one preview pane (title + thumbnail placeholder + caption) for a
    // file. When the file is a readable image and a cache is present, kicks off
    // an async thumbnail load whose result replaces the placeholder.
    QWidget* buildPane(const QString& title, const QString& path);

    Task::ConflictAnswer _answer = Task::Skip;
    bool _applyToAll = false;
    QCheckBox* _applyAllCheck = nullptr;

    ThumbnailCache* _thumbs = nullptr;
    QHash<QString, QLabel*> _panes;   // subscribed path → its image label
    QStringList _subscribed;          // to unsubscribe on close
};

#endif // CONFLICTDIALOG_H
