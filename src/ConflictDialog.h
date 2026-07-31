#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include <QDialog>
#include <QVariantMap>

#include "Task.h"

class QCheckBox;

// Modal conflict prompt shown when a task hits a destination that already
// exists (Task::DestinationExists). Replaces the old inline question strip
// that lived inside the task row: a task blocked on a conflict now surfaces
// a real blocking dialog instead of a set of buttons buried in the dock.
//
// Offers Skip / Rename / Overwrite plus an "apply to all" checkbox that maps
// to the group's sticky answer (so "Overwrite" + checkbox behaves like the
// old "Overwrite All"). The caller reads answer() / applyToAll() after
// exec() returns; Escape / close == Skip (the safe default), so the return
// code itself is not consulted.
class ConflictDialog : public QDialog {
    Q_OBJECT
public:
    ConflictDialog(int kind, const QVariantMap& context, QWidget* parent = nullptr);

    Task::ConflictAnswer answer() const { return _answer; }
    bool applyToAll() const { return _applyToAll; }

private:
    void chooseAndAccept(Task::ConflictAnswer answer);

    Task::ConflictAnswer _answer = Task::Skip;
    bool _applyToAll = false;
    QCheckBox* _applyAllCheck = nullptr;
};

#endif // CONFLICTDIALOG_H
