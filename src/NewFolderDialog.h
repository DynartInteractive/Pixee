#ifndef NEWFOLDERDIALOG_H
#define NEWFOLDERDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

// Modal "New folder" dialog. Pops up a single name field pre-filled
// with a guaranteed-unique default ("New folder", "New folder (2)",
// ...), validates against same-name collisions in `parentDir`, and
// returns the chosen name via newName(). Symmetric with RenameDialog
// in look and validation rules.
class NewFolderDialog : public QDialog {
    Q_OBJECT
public:
    NewFolderDialog(const QString& parentDir, QWidget* parent = nullptr);

    // Validated new name (after exec() returns Accepted). Empty on
    // cancel. Caller is responsible for the actual mkdir; the dialog
    // only owns the UI + validation.
    QString newName() const { return _newName; }

private slots:
    void onAccept();
    void onTextChanged();

private:
    // Picks "New folder", or "New folder (N)" with the smallest N that
    // doesn't already exist as a sibling.
    static QString pickInitialName(const QString& parentDir);

    QString _parentDir;
    QString _newName;
    QLineEdit* _edit;
    QLabel* _error;
    QPushButton* _ok;
};

#endif // NEWFOLDERDIALOG_H
