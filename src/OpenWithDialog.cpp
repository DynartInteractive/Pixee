#include "OpenWithDialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include "Config.h"
#include "OpenWithProgramDialog.h"
#include "Toast.h"

namespace {
constexpr const char* kArrayKey = "openWithPrograms";
constexpr const char* kLabelKey = "label";
constexpr const char* kPathKey  = "path";
}

void OpenWithDialog::openWithDesktop(const QStringList& filePaths,
                                     QWidget* errorParent) {
    for (const QString& path : filePaths) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            Toast::show(errorParent,
                tr("Couldn't open \"%1\"").arg(QFileInfo(path).fileName()),
                Toast::Error);
            return;   // one failure means the whole route is unavailable
        }
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
    if (filePaths.isEmpty()) return;
    // In a sandbox the stored path points at a host binary that isn't here;
    // the desktop's own chooser is the only route that works.
    if (Config::isSandboxed()) {
        openWithDesktop(filePaths, errorParent);
        return;
    }
    if (program.path.isEmpty()) return;
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
    _editBtn = new QPushButton(tr("Edit..."), this);
    _removeBtn = new QPushButton(tr("Remove"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    auto* buttons = new QVBoxLayout;
    buttons->addWidget(addBtn);
    buttons->addWidget(_editBtn);
    buttons->addWidget(_removeBtn);
    buttons->addWidget(sep);
    buttons->addWidget(closeBtn);
    buttons->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(_list, 1);
    layout->addLayout(buttons);

    connect(addBtn,     &QPushButton::clicked, this, &OpenWithDialog::onAdd);
    connect(_editBtn,   &QPushButton::clicked, this, &OpenWithDialog::onEdit);
    connect(_removeBtn, &QPushButton::clicked, this, &OpenWithDialog::onRemove);
    connect(closeBtn,   &QPushButton::clicked, this, &OpenWithDialog::accept);

    // Double-click is the habitual way into an editor, so wire it to the
    // same slot rather than leaving it dead.
    connect(_list, &QListWidget::itemDoubleClicked,
            this, &OpenWithDialog::onEdit);
    connect(_list, &QListWidget::currentRowChanged,
            this, &OpenWithDialog::updateButtons);

    _programs = loadPrograms();
    refreshList();
    updateButtons();
}

void OpenWithDialog::refreshList() {
    _list->clear();
    for (const OpenWithProgram& p : _programs) {
        auto* item = new QListWidgetItem(p.label);
        item->setToolTip(p.path);
        _list->addItem(item);
    }
}

void OpenWithDialog::updateButtons() {
    const int row = _list->currentRow();
    const bool one = row >= 0 && row < _programs.size();
    _editBtn->setEnabled(one);
    _removeBtn->setEnabled(one);
}

void OpenWithDialog::onAdd() {
    OpenWithProgramDialog dlg(QString(), QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    _programs.append({dlg.label(), dlg.path()});
    savePrograms(_programs);
    refreshList();
    _list->setCurrentRow(_programs.size() - 1);
}

void OpenWithDialog::onEdit() {
    const int row = _list->currentRow();
    if (row < 0 || row >= _programs.size()) return;

    OpenWithProgramDialog dlg(_programs.at(row).label,
                              _programs.at(row).path, this);
    if (dlg.exec() != QDialog::Accepted) return;

    _programs[row] = {dlg.label(), dlg.path()};
    savePrograms(_programs);
    refreshList();
    _list->setCurrentRow(row);   // refreshList() cleared the selection
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
    // Keep a row selected (the next one down, or the new last) so Edit and
    // Remove stay reachable for a run of deletions. refreshList()'s clear()
    // already dropped the selection to -1 and disabled them.
    if (!_programs.isEmpty()) {
        _list->setCurrentRow(qMin(row, _programs.size() - 1));
    }
}
