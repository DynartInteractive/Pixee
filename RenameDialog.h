#ifndef RENAMEDIALOG_H
#define RENAMEDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

// Modal name-edit dialog for the Rename action. Used from both the file
// list and the viewer context menus (and the F2 shortcut). Only the
// filename component is editable — caller stays in charge of the parent
// directory. Returns the new name via newName(); empty when cancelled.
//
// For files, the line edit pre-selects the base name only (extension
// stays put but visible) so typing replaces 'foo' in 'foo.jpg'. For
// folders the whole name is selected.
//
// The dialog enforces filesystem-safety up-front (no slashes, no
// reserved names) and same-folder uniqueness against parentDir before
// it accepts. Errors render inline below the field; the field stays
// focused so the user can fix and retry without reopening.
class RenameDialog : public QDialog {
    Q_OBJECT
public:
    RenameDialog(const QString& currentName,
                 const QString& parentDir,
                 bool isFolder,
                 QWidget* parent = nullptr);

    // Validated new name (after exec() returns Accepted). Empty on cancel
    // or if the user accepted with the unchanged current name (treated
    // as no-op so callers can short-circuit).
    QString newName() const { return _newName; }

private slots:
    void onAccept();
    void onTextChanged();

private:
    QString _currentName;
    QString _parentDir;
    bool _isFolder;
    QString _newName;
    QLineEdit* _edit;
    QLabel* _error;
    QPushButton* _ok;
};

#endif // RENAMEDIALOG_H
