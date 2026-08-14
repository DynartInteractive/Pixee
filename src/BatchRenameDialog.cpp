#include "BatchRenameDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "BatchRenamePlan.h"

namespace {
// Preview-row status → drives colour + whether it blocks OK.
enum class Status { Ok, Unchanged, Duplicate, Exists, Invalid };

// A file name Qt/Windows will reject outright. Kept deliberately small — the
// task still fails gracefully on anything the OS rejects, this just flags the
// obvious cases live.
bool looksIllegal(const QString& name) {
    if (name.isEmpty()) return true;
    static const QString bad = QStringLiteral("<>:\"/\\|?*");
    for (const QChar c : name)
        if (bad.contains(c) || c.unicode() < 0x20) return true;
    return name == QStringLiteral(".") || name == QStringLiteral("..");
}
}  // namespace

BatchRenameDialog::BatchRenameDialog(const QStringList& sourcePaths, QWidget* parent)
    : QDialog(parent), _sources(sourcePaths) {
    setModal(true);
    setWindowTitle(tr("Batch rename"));
    if (!_sources.isEmpty())
        _dir = QFileInfo(_sources.first()).absolutePath();

    // ---- transform controls ----
    auto* form = new QFormLayout();
    _find = new QLineEdit(this);
    _replace = new QLineEdit(this);
    _caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    auto* findRow = new QWidget(this);
    auto* findLayout = new QVBoxLayout(findRow);
    findLayout->setContentsMargins(0, 0, 0, 0);
    findLayout->setSpacing(4);
    findLayout->addWidget(_replace);
    findLayout->addWidget(_caseSensitive);
    form->addRow(tr("Find:"), _find);
    form->addRow(tr("Replace with:"), findRow);

    _pattern = new QLineEdit(this);
    _pattern->setText(QStringLiteral("{name}"));
    _pattern->setToolTip(tr("Tokens: {name} = original name, {n} = number, "
                            "{n:3} = zero-padded to 3 digits.\n"
                            "Prefix/suffix by typing around {name}, e.g. IMG_{name}."));
    form->addRow(tr("Pattern:"), _pattern);

    _start = new QSpinBox(this);
    _start->setRange(0, 1'000'000);
    _start->setValue(1);
    _step = new QSpinBox(this);
    _step->setRange(1, 10'000);
    _step->setValue(1);
    auto* numRow = new QWidget(this);
    auto* numLayout = new QHBoxLayout(numRow);
    numLayout->setContentsMargins(0, 0, 0, 0);
    numLayout->addWidget(new QLabel(tr("Start:"), this));
    numLayout->addWidget(_start);
    numLayout->addSpacing(12);
    numLayout->addWidget(new QLabel(tr("Step:"), this));
    numLayout->addWidget(_step);
    numLayout->addStretch(1);
    form->addRow(tr("Numbering:"), numRow);

    _keepExt = new QCheckBox(tr("Keep file extension"), this);
    _keepExt->setChecked(true);
    form->addRow(QString(), _keepExt);

    // ---- preview table ----
    _table = new QTableWidget(this);
    _table->setColumnCount(2);
    _table->setHorizontalHeaderLabels({ tr("Original"), tr("New name") });
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->verticalHeader()->setVisible(false);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    _table->setRowCount(_sources.size());

    _summary = new QLabel(this);
    _summary->setObjectName("batchRenameSummary");

    // ---- buttons ----
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    _okButton = buttons->addButton(tr("Rename"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_okButton, &QPushButton::clicked, this, &BatchRenameDialog::onAccept);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_table, 1);
    layout->addWidget(_summary);
    layout->addWidget(buttons);
    resize(560, 520);

    // Live preview on any change.
    connect(_find, &QLineEdit::textChanged, this, &BatchRenameDialog::recompute);
    connect(_replace, &QLineEdit::textChanged, this, &BatchRenameDialog::recompute);
    connect(_caseSensitive, &QCheckBox::toggled, this, &BatchRenameDialog::recompute);
    connect(_pattern, &QLineEdit::textChanged, this, &BatchRenameDialog::recompute);
    connect(_start, QOverload<int>::of(&QSpinBox::valueChanged), this, &BatchRenameDialog::recompute);
    connect(_step, QOverload<int>::of(&QSpinBox::valueChanged), this, &BatchRenameDialog::recompute);
    connect(_keepExt, &QCheckBox::toggled, this, &BatchRenameDialog::recompute);

    recompute();
}

void BatchRenameDialog::recompute() {
    BatchRename::Options opts;
    opts.findText = _find->text();
    opts.replaceText = _replace->text();
    opts.caseSensitive = _caseSensitive->isChecked();
    opts.pattern = _pattern->text();
    opts.startNumber = _start->value();
    opts.step = _step->value();
    opts.keepExtension = _keepExt->isChecked();

    const int n = _sources.size();
    QStringList oldNames, newNames;
    oldNames.reserve(n);
    newNames.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QString oldName = QFileInfo(_sources.at(i)).fileName();
        oldNames << oldName;
        newNames << BatchRename::newNameFor(oldName, opts, i);
    }

    // Names of files that are actually changing — used to tell a "clash with a
    // sibling that will be vacated" (fine, ordering handles it) from a clash
    // with one we're keeping (a real overwrite the task must prompt for).
    QSet<QString> vacatedOldNames;
    QHash<QString, int> newNameCounts;  // among changing rows only
    for (int i = 0; i < n; ++i) {
        if (newNames.at(i) == oldNames.at(i)) continue;  // unchanged
        vacatedOldNames.insert(oldNames.at(i));
        newNameCounts[newNames.at(i)]++;
    }

    int changing = 0, hardProblems = 0, willPrompt = 0;
    for (int i = 0; i < n; ++i) {
        const QString& oldName = oldNames.at(i);
        const QString& newName = newNames.at(i);
        Status st;
        if (newName == oldName) {
            st = Status::Unchanged;
        } else if (looksIllegal(newName)) {
            st = Status::Invalid;
        } else if (newNameCounts.value(newName) > 1) {
            st = Status::Duplicate;
        } else if (QFileInfo::exists(QDir(_dir).filePath(newName))
                   && !vacatedOldNames.contains(newName)) {
            st = Status::Exists;  // an on-disk file we are NOT vacating
        } else {
            st = Status::Ok;
        }

        if (st != Status::Unchanged) ++changing;
        if (st == Status::Duplicate || st == Status::Invalid) ++hardProblems;
        if (st == Status::Exists) ++willPrompt;

        auto* c0 = new QTableWidgetItem(oldName);
        auto* c1 = new QTableWidgetItem(newName);
        QColor fg;
        switch (st) {
            case Status::Unchanged: fg = QColor(0x88, 0x88, 0x88); break;
            case Status::Duplicate:
            case Status::Invalid:   fg = QColor(0xe0, 0x50, 0x50); break;  // red
            case Status::Exists:    fg = QColor(0xd6, 0x9e, 0x2e); break;  // amber
            case Status::Ok:        break;  // theme default
        }
        if (fg.isValid()) { c0->setForeground(fg); c1->setForeground(fg); }
        if (st == Status::Exists)
            c1->setToolTip(tr("A file named \"%1\" already exists — you'll be "
                              "asked to skip / overwrite / rename.").arg(newName));
        else if (st == Status::Duplicate)
            c1->setToolTip(tr("Two files would get this same name."));
        _table->setItem(i, 0, c0);
        _table->setItem(i, 1, c1);
    }

    QStringList parts;
    parts << tr("%n file(s) selected", nullptr, n);
    parts << tr("%n will be renamed", nullptr, changing);
    if (willPrompt > 0) parts << tr("%n already exist", nullptr, willPrompt);
    if (hardProblems > 0) parts << tr("%n name clash(es)", nullptr, hardProblems);
    _summary->setText(parts.join(QStringLiteral(" · ")));

    _okButton->setEnabled(changing > 0 && hardProblems == 0);
}

void BatchRenameDialog::onAccept() {
    BatchRename::Options opts;
    opts.findText = _find->text();
    opts.replaceText = _replace->text();
    opts.caseSensitive = _caseSensitive->isChecked();
    opts.pattern = _pattern->text();
    opts.startNumber = _start->value();
    opts.step = _step->value();
    opts.keepExtension = _keepExt->isChecked();

    _accepted.clear();
    for (int i = 0; i < _sources.size(); ++i) {
        const QString oldName = QFileInfo(_sources.at(i)).fileName();
        const QString newName = BatchRename::newNameFor(oldName, opts, i);
        if (newName == oldName || looksIllegal(newName)) continue;
        _accepted.append({ _sources.at(i), QDir(_dir).filePath(newName) });
    }
    accept();
}
