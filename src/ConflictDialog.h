#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include <QDialog>
#include <QList>
#include <QSet>
#include <QString>
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
//
// Thumbnails load two ways: first through the shared ThumbnailCache (instant
// when the file was just browsed; the async thumbnailReady arrives while
// exec() spins its nested loop), and then, for any pane the cache hasn't
// filled shortly after, a direct synchronous decode from disk. The fallback
// matters because the cache can't always deliver here — the generator is
// paused while the viewer is open, and copying a file onto itself makes both
// panes share one path.
//
// Offers Skip / Rename / Overwrite plus an "apply to all" checkbox that maps
// to the group's sticky answer (so "Overwrite" + checkbox behaves like the
// old "Overwrite All"). The caller reads answer() / applyToAll() after
// exec() returns; Escape / close == Skip (the safe default), so the return
// code itself is not consulted.
class ConflictDialog : public QDialog {
    Q_OBJECT
public:
    // thumbs may be null (e.g. in tests) — panes then load purely from disk.
    ConflictDialog(int kind, const QVariantMap& context,
                   ThumbnailCache* thumbs, QWidget* parent = nullptr);
    ~ConflictDialog() override;

    Task::ConflictAnswer answer() const { return _answer; }
    bool applyToAll() const { return _applyToAll; }

private slots:
    void onThumbnailReady(const QString& path, const QImage& image);
    // Synchronous fallback: decode from disk any pane the cache left empty.
    void fillMissingFromDisk();

private:
    void chooseAndAccept(Task::ConflictAnswer answer);
    // Build one preview pane (title + thumbnail placeholder + caption) for a
    // file, registering it so both the async cache result and the disk
    // fallback can fill it. Subscribes to the cache once per unique path.
    QWidget* buildPane(const QString& title, const QString& path);
    void setPaneImage(QLabel* label, const QImage& image);

    // One preview slot. Several panes can share a path (copy-onto-self), so
    // this is a flat list, not a path→label map.
    struct Pane {
        QString path;
        QLabel* label;
        bool    isImage;
        bool    loaded;
    };

    Task::ConflictAnswer _answer = Task::Skip;
    bool _applyToAll = false;
    QCheckBox* _applyAllCheck = nullptr;

    ThumbnailCache* _thumbs = nullptr;
    QList<Pane>   _panes;
    QSet<QString> _subscribed;  // unique cache subscriptions, to unsubscribe on close
};

#endif // CONFLICTDIALOG_H
