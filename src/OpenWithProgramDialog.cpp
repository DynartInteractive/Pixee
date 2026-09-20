#include "OpenWithProgramDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Sensible starting point for the Browse picker. Windows registers the
// canonical path through the ProgramFiles env var (handles localised
// 'Program Files (x86)' / non-C: installs); fall back to the conventional
// path if the env var is empty (some stripped sandboxes).
QString defaultProgramsFolder() {
#ifdef Q_OS_WIN
    const QString pf = qEnvironmentVariable("ProgramFiles");
    return pf.isEmpty() ? QStringLiteral("C:/Program Files") : pf;
#else
    return QStringLiteral("/usr/bin");
#endif
}

// Empty return == the pair is good to save.
QString validate(const QString& label, const QString& path) {
    if (label.trimmed().isEmpty()) {
        return QObject::tr("Enter a name to show in the menu.");
    }
    const QString p = path.trimmed();
    if (p.isEmpty()) {
        return QObject::tr("Enter or browse for the program's executable.");
    }
    // A bare command name carries no separator; QProcess::startDetached
    // resolves that against PATH, so there is nothing to check on disk.
    // Anything with a separator is a real path and has to be there.
    if (p.contains('/') || p.contains('\\')) {
        const QFileInfo info(p);
        if (!info.exists()) {
            return QObject::tr("No such file: %1").arg(QDir::toNativeSeparators(p));
        }
        if (info.isDir()) {
            return QObject::tr("That's a folder, not a program.");
        }
    }
    return QString();
}
}

OpenWithProgramDialog::OpenWithProgramDialog(const QString& label,
                                             const QString& path,
                                             QWidget* parent)
    : QDialog(parent) {
    const bool isEdit = !(label.isEmpty() && path.isEmpty());
    setWindowTitle(isEdit ? tr("Edit program") : tr("Add program"));
    setModal(true);

    _labelEdit = new QLineEdit(label, this);
    _labelEdit->setPlaceholderText(tr("Name shown in the Open with menu"));

    _pathEdit = new QLineEdit(path, this);
    _pathEdit->setMinimumWidth(360);

    auto* browseBtn = new QPushButton(tr("Browse..."), this);
    browseBtn->setAutoDefault(false);   // Enter belongs to Save, not Browse

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(_pathEdit, 1);
    pathRow->addWidget(browseBtn);

    auto* form = new QFormLayout;
    form->addRow(tr("Label:"), _labelEdit);
    form->addRow(tr("Program:"), pathRow);

    _error = new QLabel(this);
    _error->setStyleSheet("color: #c53030;");
    _error->setWordWrap(true);
    _error->setVisible(false);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    _save = buttons->button(QDialogButtonBox::Save);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &OpenWithProgramDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_error);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked,
            this, &OpenWithProgramDialog::onBrowse);
    connect(_labelEdit, &QLineEdit::textChanged,
            this, &OpenWithProgramDialog::onFieldChanged);
    connect(_pathEdit, &QLineEdit::textChanged,
            this, &OpenWithProgramDialog::onFieldChanged);

    onFieldChanged();   // initial validation pass
    _labelEdit->setFocus();
    _labelEdit->selectAll();
}

QString OpenWithProgramDialog::label() const {
    return _labelEdit->text().trimmed();
}

QString OpenWithProgramDialog::path() const {
    return _pathEdit->text().trimmed();
}

void OpenWithProgramDialog::onBrowse() {
#ifdef Q_OS_WIN
    const QString filter = tr("Programs (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    // Start where the field already points, so re-picking within the same
    // install folder doesn't mean navigating there from scratch.
    QString startDir = defaultProgramsFolder();
    const QString current = _pathEdit->text().trimmed();
    if (!current.isEmpty()) {
        const QFileInfo info(current);
        if (info.isDir()) {
            startDir = info.absoluteFilePath();
        } else if (info.absoluteDir().exists()) {
            startDir = info.absolutePath();
        }
    }

    const QString picked = QFileDialog::getOpenFileName(
        this, tr("Pick a program"), startDir, filter);
    if (picked.isEmpty()) return;

    _pathEdit->setText(QDir::toNativeSeparators(picked));
    // Fill an empty label from the file name — usually exactly what you'd
    // have typed. Never overwrite a label the user already has.
    if (_labelEdit->text().trimmed().isEmpty()) {
        _labelEdit->setText(QFileInfo(picked).completeBaseName());
    }
}

void OpenWithProgramDialog::onFieldChanged() {
    const QString err = validate(_labelEdit->text(), _pathEdit->text());
    if (err.isEmpty()) {
        _error->clear();
        _error->setVisible(false);
        _save->setEnabled(true);
    } else {
        _error->setText(err);
        _error->setVisible(true);
        _save->setEnabled(false);
    }
}

void OpenWithProgramDialog::onAccept() {
    const QString err = validate(_labelEdit->text(), _pathEdit->text());
    if (!err.isEmpty()) {
        // Defensive — onFieldChanged should have disabled Save already, but
        // this covers any route that bypasses textChanged.
        _error->setText(err);
        _error->setVisible(true);
        _save->setEnabled(false);
        return;
    }
    accept();
}
