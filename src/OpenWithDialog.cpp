#include "OpenWithDialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "Toast.h"

namespace {
constexpr const char* kArrayKey = "openWithPrograms";
constexpr const char* kLabelKey = "label";
constexpr const char* kPathKey  = "path";

// Sensible starting point for the Add file picker. Windows registers the
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
}

QList<OpenWithProgram> OpenWithDialog::loadPrograms() {
    QSettings settings;
    QList<OpenWithProgram> out;
    const int count = settings.beginReadArray(kArrayKey);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        OpenWithProgram p;
        p.label = settings.value(kLabelKey).toString();
        p.path  = settings.value(kPathKey).toString();
        if (!p.label.isEmpty() && !p.path.isEmpty()) out.append(p);
    }
    settings.endArray();
    return out;
}

void OpenWithDialog::savePrograms(const QList<OpenWithProgram>& programs) {
    QSettings settings;
    // remove() before begin*Array() so a shrunk list doesn't leave stale
    // higher-indexed entries behind (QSettings doesn't trim arrays on its own).
    settings.remove(kArrayKey);
    settings.beginWriteArray(kArrayKey);
    for (int i = 0; i < programs.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(kLabelKey, programs.at(i).label);
        settings.setValue(kPathKey,  programs.at(i).path);
    }
    settings.endArray();
}

void OpenWithDialog::launch(const OpenWithProgram& program,
                            const QStringList& filePaths,
                            QWidget* errorParent) {
    if (program.path.isEmpty() || filePaths.isEmpty()) return;
    if (!QProcess::startDetached(program.path, filePaths)) {
        Toast::show(errorParent,
            tr("Failed to launch \"%1\"").arg(program.label),
            Toast::Error);
    }
}

OpenWithDialog::OpenWithDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configure programs"));
    setModal(true);

    _list = new QListWidget(this);
    _list->setMinimumWidth(280);

    auto* addBtn = new QPushButton(tr("Add..."), this);
    auto* removeBtn = new QPushButton(tr("Remove"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    auto* buttons = new QVBoxLayout;
    buttons->addWidget(addBtn);
    buttons->addWidget(removeBtn);
    buttons->addWidget(sep);
    buttons->addWidget(closeBtn);
    buttons->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(_list, 1);
    layout->addLayout(buttons);

    connect(addBtn,    &QPushButton::clicked, this, &OpenWithDialog::onAdd);
    connect(removeBtn, &QPushButton::clicked, this, &OpenWithDialog::onRemove);
    connect(closeBtn,  &QPushButton::clicked, this, &OpenWithDialog::accept);

    _programs = loadPrograms();
    refreshList();
}

void OpenWithDialog::refreshList() {
    _list->clear();
    for (const OpenWithProgram& p : _programs) {
        auto* item = new QListWidgetItem(p.label);
        item->setToolTip(p.path);
        _list->addItem(item);
    }
}

void OpenWithDialog::onAdd() {
#ifdef Q_OS_WIN
    const QString filter = tr("Programs (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Pick a program"), defaultProgramsFolder(), filter);
    if (path.isEmpty()) return;

    bool ok = false;
    const QString defaultLabel = QFileInfo(path).completeBaseName();
    const QString label = QInputDialog::getText(
        this, tr("Program label"), tr("Display name:"),
        QLineEdit::Normal, defaultLabel, &ok);
    if (!ok) return;
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty()) return;

    _programs.append({trimmed, path});
    savePrograms(_programs);
    refreshList();
    _list->setCurrentRow(_programs.size() - 1);
}

void OpenWithDialog::onRemove() {
    const int row = _list->currentRow();
    if (row < 0 || row >= _programs.size()) return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove program"),
        tr("Remove \"%1\" from the list?").arg(_programs.at(row).label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    _programs.removeAt(row);
    savePrograms(_programs);
    refreshList();
}
