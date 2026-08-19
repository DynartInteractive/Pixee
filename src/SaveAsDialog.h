#ifndef SAVEASDIALOG_H
#define SAVEASDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

// Modal "Save As…" dialog: pick a destination folder, a base name, an output
// format, and (for lossy formats) a quality. It's the front end to a
// ConvertFormatTask — the actual decode/encode runs through the task pipeline,
// so this dialog only gathers the three values and validates the target path.
//
// The folder is chosen with the same native picker Copy to / Move to use
// (QFileDialog::getExistingDirectory), remembering the last folder in
// QSettings("fileOps/lastSaveAsPath"). Format choices come from
// Config::writableImageFormats(), so only encodable formats are ever offered.
//
// This round Save As converts from the file on disk (there are no in-memory
// edits yet); when the editing ops land, the caller can hand the edited image
// straight to the task instead.
class SaveAsDialog : public QDialog {
    Q_OBJECT
public:
    // sourcePath seeds the defaults: its folder, its base name, and its format
    // (when that format is writable; otherwise the first writable format).
    SaveAsDialog(const QString& sourcePath,
                 const QStringList& writableFormats,
                 QWidget* parent = nullptr);

    // Valid after exec() returns Accepted.
    QString    destPath() const;   // <folder>/<name>.<ext>, native-ish separators
    QByteArray format() const;     // lowercase extension, e.g. "png", "jpg"
    int        quality() const;    // 0..100; meaningful only for lossy formats

private slots:
    void chooseFolder();
    void onFormatChanged();
    void validate();     // enable/disable OK and refresh the target-path label

private:
    static bool formatIsLossy(const QString& fmt);

    QString _sourcePath;
    QString _folder;

    QLineEdit*   _folderEdit = nullptr;
    QLineEdit*   _nameEdit = nullptr;
    QComboBox*   _formatCombo = nullptr;
    QLabel*      _qualityLabel = nullptr;
    QSlider*     _qualitySlider = nullptr;
    QLabel*      _qualityValue = nullptr;
    QLabel*      _targetLabel = nullptr;
    QPushButton* _okButton = nullptr;
};

#endif // SAVEASDIALOG_H
