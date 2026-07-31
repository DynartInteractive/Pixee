#include "NewFolderDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const QString kInvalidChars = QStringLiteral("/\\:*?\"<>|");

QString validate(const QString& proposed, const QString& parentDir) {
    if (proposed.isEmpty()) {
        return QObject::tr("Name cannot be empty.");
    }
    if (proposed.trimmed().isEmpty()) {
        return QObject::tr("Name cannot be only whitespace.");
    }
    if (proposed == "." || proposed == "..") {
        return QObject::tr("Reserved name.");
    }
    for (QChar c : kInvalidChars) {
        if (proposed.contains(c)) {
            return QObject::tr("Name cannot contain: %1").arg(kInvalidChars);
        }
    }
    if (QFileInfo::exists(QDir(parentDir).filePath(proposed))) {
        return QObject::tr("A file or folder named \"%1\" already exists here.")
                   .arg(proposed);
    }
    return QString();
}
}

QString NewFolderDialog::pickInitialName(const QString& parentDir) {
    const QString base = QObject::tr("New folder");
    QDir dir(parentDir);
    if (!QFileInfo::exists(dir.filePath(base))) return base;
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!QFileInfo::exists(dir.filePath(candidate))) return candidate;
    }
    // Pathological — give up and return the base name; the user can
    // still edit before accepting.
    return base;
}

NewFolderDialog::NewFolderDialog(const QString& parentDir, QWidget* parent)
    : QDialog(parent), _parentDir(parentDir) {
    setWindowTitle(tr("New folder"));
    setModal(true);

    auto* nameLabel = new QLabel(tr("Folder name:"), this);
    _edit = new QLineEdit(pickInitialName(parentDir), this);
    _edit->setMinimumWidth(360);
    _edit->selectAll();

    _error = new QLabel(this);
    _error->setStyleSheet("color: #c53030;");
    _error->setWordWrap(true);
    _error->setVisible(false);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _ok = buttons->button(QDialogButtonBox::Ok);
    _ok->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &NewFolderDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(nameLabel);
    layout->addWidget(_edit);
    layout->addWidget(_error);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(_edit, &QLineEdit::textChanged, this, &NewFolderDialog::onTextChanged);
    onTextChanged();
}

void NewFolderDialog::onTextChanged() {
    const QString err = validate(_edit->text(), _parentDir);
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

void NewFolderDialog::onAccept() {
    const QString text = _edit->text();
    const QString err = validate(text, _parentDir);
    if (!err.isEmpty()) {
        _error->setText(err);
        _error->setVisible(true);
        _ok->setEnabled(false);
        return;
    }
    _newName = text;
    accept();
}
