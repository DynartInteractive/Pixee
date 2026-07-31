#ifndef OPENWITHDIALOG_H
#define OPENWITHDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QListWidget;

struct OpenWithProgram {
    QString label;
    QString path;
};

class OpenWithDialog : public QDialog {
    Q_OBJECT
public:
    explicit OpenWithDialog(QWidget* parent = nullptr);

    // Persisted list of user-configured external programs. Stored under
    // QSettings array key 'openWithPrograms'. Both menus (file-list and
    // viewer context menus) read this list to populate the 'Open with'
    // submenu; the dialog is the only writer.
    static QList<OpenWithProgram> loadPrograms();
    static void savePrograms(const QList<OpenWithProgram>& programs);

    // Spawn `program` with the given file paths as command-line arguments,
    // detached. Pixee does not wait for the child. Shows a Toast::Error on
    // `errorParent` if QProcess::startDetached returns false (unreadable
    // executable, missing file, etc.).
    static void launch(const OpenWithProgram& program,
                       const QStringList& filePaths,
                       QWidget* errorParent);

private slots:
    void onAdd();
    void onRemove();

private:
    void refreshList();

    QListWidget* _list;
    QList<OpenWithProgram> _programs;
};

#endif // OPENWITHDIALOG_H
