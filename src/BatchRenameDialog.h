#ifndef BATCHRENAMEDIALOG_H
#define BATCHRENAMEDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Modal batch-rename dialog. Given the selected file paths it offers three
// composable operations — find & replace, a {name}/{n} name pattern (prefix,
// suffix and sequential numbering all fall out of the pattern), and keep-
// extension — with a live before→after preview table that highlights
// collisions. On accept, renames() returns the old→new absolute-path pairs for
// the files whose name actually changed; the caller orders them safely
// (BatchRename::planRenameSteps) and enqueues RenameTasks.
class BatchRenameDialog : public QDialog {
    Q_OBJECT
public:
    BatchRenameDialog(const QStringList& sourcePaths, QWidget* parent = nullptr);

    // Valid after exec() returns Accepted. Absolute (oldPath, newPath) pairs,
    // one per file whose name changed (unchanged rows are omitted).
    QList<QPair<QString, QString>> renames() const { return _accepted; }

private slots:
    void recompute();     // rebuild the preview from the current controls
    void onAccept();      // stash the accepted renames, then accept()

private:
    QStringList _sources;   // absolute source paths, in list order
    QString     _dir;       // common parent directory

    QLineEdit*    _find = nullptr;
    QLineEdit*    _replace = nullptr;
    QCheckBox*    _caseSensitive = nullptr;
    QLineEdit*    _pattern = nullptr;
    QSpinBox*     _start = nullptr;
    QSpinBox*     _step = nullptr;
    QCheckBox*    _keepExt = nullptr;
    QTableWidget* _table = nullptr;
    QLabel*       _summary = nullptr;
    QPushButton*  _okButton = nullptr;

    QList<QPair<QString, QString>> _accepted;
};

#endif // BATCHRENAMEDIALOG_H
