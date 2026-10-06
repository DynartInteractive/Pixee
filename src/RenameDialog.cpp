#include "RenameDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "FileOpsHelpers.h"

namespace {
QString validate(const QString& proposed,
                 const QString& currentName,
                 const QString& parentDir,
                 bool isFolder) {
    if (proposed == currentName) {
        // Caller treats this as no-op cancel — return empty (no error)
        // so the OK button stays enabled, but newName() will be empty.
        return QString();
    }
    const QString problem = FileOpsHelpers::fileNameProblem(proposed);
    if (!problem.isEmpty()) return problem;
    // Same-folder uniqueness. On a case-insensitive filesystem a case-only
    // change ("img.jpg" → "IMG.jpg") "exists" only as the item itself —
    // that's a legitimate rename, not a clash.
    const QString destPath = QDir(parentDir).filePath(proposed);
    if (QFileInfo::exists(destPath)
            && !FileOpsHelpers::isSameFile(destPath,
                                           QDir(parentDir).filePath(currentName))) {
        return isFolder
            ? QObject::tr("A file or folder named \"%1\" already exists here.").arg(proposed)
            : QObject::tr("A file or folder named \"%1\" already exists here.").arg(proposed);
    }
    return QString();
}
}

RenameDialog::RenameDialog(const QString& currentName,
                           const QString& parentDir,
                           bool isFolder,
                           QWidget* parent)
    : QDialog(parent),
      _currentName(currentName),
      _parentDir(parentDir),
      _isFolder(isFolder) {
    setWindowTitle(isFolder ? tr("Rename folder") : tr("Rename file"));
    setModal(true);

    auto* nameLabel = new QLabel(tr("New name:"), this);
    _edit = new QLineEdit(currentName, this);
    _edit->setMinimumWidth(360);

    // For files, pre-select base name only so typing replaces 'foo' in
    // 'foo.jpg' but the extension survives. For folders (no real
    // extension concept), select all.
    if (!isFolder) {
        const int dot = currentName.lastIndexOf('.');
        if (dot > 0) {
            _edit->setSelection(0, dot);
        } else {
            _edit->selectAll();
        }
    } else {
        _edit->selectAll();
    }

    _error = new QLabel(this);
    _error->setStyleSheet("color: #c53030;");
    _error->setWordWrap(true);
    _error->setVisible(false);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _ok = buttons->button(QDialogButtonBox::Ok);
    _ok->setText(tr("Rename"));
    connect(buttons, &QDialogButtonBox::accepted, this, &RenameDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(nameLabel);
    layout->addWidget(_edit);
    layout->addWidget(_error);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(_edit, &QLineEdit::textChanged, this, &RenameDialog::onTextChanged);
    onTextChanged();  // initial validation pass
}

void RenameDialog::onTextChanged() {
    const QString text = _edit->text();
    const QString err = validate(text, _currentName, _parentDir, _isFolder);
    if (err.isEmpty()) {
        _error->clear();
        _error->setVisible(false);
        _ok->setEnabled(true);
    } else {
        _error->setText(err);
        _error->setVisible(true);
        _ok->setEnabled(false);
    }
}

void RenameDialog::onAccept() {
    const QString text = _edit->text();
    const QString err = validate(text, _currentName, _parentDir, _isFolder);
    if (!err.isEmpty()) {
        // Defensive — onTextChanged should have disabled OK already, but
        // belt-and-suspenders covers any race with paste / programmatic
        // setText that might bypass textChanged.
        _error->setText(err);
        _error->setVisible(true);
        _ok->setEnabled(false);
        return;
    }
    // Treat unchanged-name as a no-op cancel — newName() returns empty
    // so the caller can skip the disk op without separate accept/reject
    // branching at the call site.
    _newName = (text == _currentName) ? QString() : text;
    accept();
}
