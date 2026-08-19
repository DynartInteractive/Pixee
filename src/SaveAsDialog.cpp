#include "SaveAsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace {
constexpr const char* kLastSaveAsPath = "fileOps/lastSaveAsPath";
constexpr int kDefaultQuality = 92;
}  // namespace

SaveAsDialog::SaveAsDialog(const QString& sourcePath,
                           const QStringList& writableFormats,
                           QWidget* parent)
    : QDialog(parent),
      _sourcePath(sourcePath) {
    setWindowTitle(tr("Save As"));
    setModal(true);

    const QFileInfo srcInfo(sourcePath);

    // Default folder: last-used Save As folder, else the source's folder.
    _folder = QSettings().value(kLastSaveAsPath).toString();
    if (_folder.isEmpty() || !QDir(_folder).exists())
        _folder = srcInfo.absolutePath();

    auto* form = new QFormLayout;

    // --- Folder row: read-only path + Change… (native picker) --------------
    _folderEdit = new QLineEdit(QDir::toNativeSeparators(_folder));
    _folderEdit->setReadOnly(true);
    auto* changeBtn = new QPushButton(tr("Change..."));
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(_folderEdit, 1);
    folderRow->addWidget(changeBtn);
    form->addRow(tr("Folder:"), folderRow);

    // --- Name (base, no extension) -----------------------------------------
    _nameEdit = new QLineEdit(srcInfo.completeBaseName());
    form->addRow(tr("Name:"), _nameEdit);

    // --- Format dropdown (writable formats only) ---------------------------
    _formatCombo = new QComboBox;
    for (const QString& fmt : writableFormats)
        _formatCombo->addItem(fmt.toUpper(), fmt);
    // Preselect the source's own format when we can write it; else the first.
    const QString srcExt = srcInfo.suffix().toLower();
    int srcIdx = _formatCombo->findData(srcExt);
    if (srcIdx < 0 && srcExt == "jpeg") srcIdx = _formatCombo->findData(QStringLiteral("jpg"));
    _formatCombo->setCurrentIndex(srcIdx >= 0 ? srcIdx : 0);
    form->addRow(tr("Format:"), _formatCombo);

    // --- Quality (lossy formats only) --------------------------------------
    _qualitySlider = new QSlider(Qt::Horizontal);
    _qualitySlider->setRange(1, 100);
    _qualitySlider->setValue(kDefaultQuality);
    _qualityValue = new QLabel(QString::number(kDefaultQuality));
    _qualityValue->setMinimumWidth(28);
    auto* qualityRow = new QHBoxLayout;
    qualityRow->addWidget(_qualitySlider, 1);
    qualityRow->addWidget(_qualityValue);
    _qualityLabel = new QLabel(tr("Quality:"));
    form->addRow(_qualityLabel, qualityRow);

    // --- Target-path preview + buttons -------------------------------------
    _targetLabel = new QLabel;
    _targetLabel->setObjectName("saveAsTarget");
    _targetLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    _okButton = buttons->button(QDialogButtonBox::Save);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_targetLabel);
    layout->addWidget(buttons);
    setMinimumWidth(440);

    connect(changeBtn, &QPushButton::clicked, this, &SaveAsDialog::chooseFolder);
    connect(_nameEdit, &QLineEdit::textChanged, this, &SaveAsDialog::validate);
    connect(_formatCombo, &QComboBox::currentIndexChanged, this, &SaveAsDialog::onFormatChanged);
    connect(_qualitySlider, &QSlider::valueChanged, this, [this](int v) {
        _qualityValue->setText(QString::number(v));
    });

    onFormatChanged();   // set initial quality-row visibility + validate
}

bool SaveAsDialog::formatIsLossy(const QString& fmt) {
    const QString f = fmt.toLower();
    return f == "jpg" || f == "jpeg" || f == "webp";
}

QByteArray SaveAsDialog::format() const {
    return _formatCombo->currentData().toString().toLatin1();
}

int SaveAsDialog::quality() const {
    return _qualitySlider->value();
}

QString SaveAsDialog::destPath() const {
    const QString ext = QString::fromLatin1(format());
    const QString name = _nameEdit->text().trimmed();
    return QDir(_folder).filePath(name + "." + ext);
}

void SaveAsDialog::chooseFolder() {
    const QString picked = QFileDialog::getExistingDirectory(
        this, tr("Choose destination folder"), _folder,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) return;
    _folder = picked;
    _folderEdit->setText(QDir::toNativeSeparators(_folder));
    QSettings().setValue(kLastSaveAsPath, _folder);
    validate();
}

void SaveAsDialog::onFormatChanged() {
    const bool lossy = formatIsLossy(_formatCombo->currentData().toString());
    _qualityLabel->setVisible(lossy);
    _qualitySlider->setVisible(lossy);
    _qualityValue->setVisible(lossy);
    validate();
}

void SaveAsDialog::validate() {
    const QString name = _nameEdit->text().trimmed();
    const bool ok = !name.isEmpty() && !_folder.isEmpty();
    if (_okButton) _okButton->setEnabled(ok);

    if (ok) {
        _targetLabel->setText(tr("Save to: %1")
                                  .arg(QDir::toNativeSeparators(destPath())));
    } else {
        _targetLabel->setText(tr("Enter a file name."));
    }
}
