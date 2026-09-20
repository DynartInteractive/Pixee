#ifndef OPENWITHPROGRAMDIALOG_H
#define OPENWITHPROGRAMDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

// Modal editor for a single 'Open with' entry: the label shown in the menu
// and the path to the executable behind it. Used for both Add and Edit from
// OpenWithDialog — the only difference is the window title and whether the
// fields start populated, so the two paths can't drift apart.
//
// Save stays disabled until both fields hold something usable, with the
// reason rendered inline the way RenameDialog does it. A bare command name
// with no directory separator is allowed on purpose: QProcess resolves it
// against PATH, which is how you'd configure 'gimp' on Linux. Anything that
// looks like a path has to exist and be a file.
//
// Takes and returns plain strings rather than an OpenWithProgram so it
// doesn't have to include the list dialog's header.
class OpenWithProgramDialog : public QDialog {
    Q_OBJECT
public:
    // Both empty == Add; anything populated == Edit.
    OpenWithProgramDialog(const QString& label,
                          const QString& path,
                          QWidget* parent = nullptr);

    // Valid only after exec() returns Accepted. Both are trimmed.
    QString label() const;
    QString path() const;

private slots:
    void onBrowse();
    void onFieldChanged();
    void onAccept();

private:
    QLineEdit* _labelEdit;
    QLineEdit* _pathEdit;
    QLabel* _error;
    QPushButton* _save;
};

#endif // OPENWITHPROGRAMDIALOG_H
