#include "ConflictDialog.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

ConflictDialog::ConflictDialog(int kind, const QVariantMap& context, QWidget* parent)
    : QDialog(parent) {
    // Application-modal: the whole app blocks until the user answers, which is
    // the point — the worker thread is parked on a wait condition and the dock
    // controls (pause / stop) would race an answer if they stayed live.
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(tr("File already exists"));

    const QString src = context.value("src").toString();
    const QString dst = context.value("dst").toString();
    const QString dstName = QFileInfo(dst).fileName();
    const QString dstDir = QFileInfo(dst).absolutePath();
    const QString srcDir = QFileInfo(src).absolutePath();

    // ---- icon + primary/secondary text ----
    auto* top = new QHBoxLayout();
    top->setSpacing(12);

    auto* icon = new QLabel(this);
    const int px = style()->pixelMetric(QStyle::PM_MessageBoxIconSize);
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(px, px));
    icon->setAlignment(Qt::AlignTop);
    top->addWidget(icon, 0);

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(6);

    QString primary = (kind == Task::DestinationExists && !dstName.isEmpty())
        ? tr("\"%1\" already exists in the destination folder.").arg(dstName)
        : tr("The destination already exists.");
    auto* primaryLabel = new QLabel(primary, this);
    primaryLabel->setWordWrap(true);
    QFont f = primaryLabel->font();
    f.setBold(true);
    primaryLabel->setFont(f);
    textCol->addWidget(primaryLabel);

    if (!dstDir.isEmpty() || !srcDir.isEmpty()) {
        auto* detail = new QLabel(this);
        detail->setObjectName("conflictDetail");
        detail->setTextFormat(Qt::PlainText);
        detail->setWordWrap(true);
        QStringList lines;
        if (!dstDir.isEmpty()) lines << tr("Destination folder: %1").arg(dstDir);
        if (!srcDir.isEmpty()) lines << tr("Source folder: %1").arg(srcDir);
        detail->setText(lines.join('\n'));
        textCol->addWidget(detail);
    }

    top->addLayout(textCol, 1);

    // ---- apply-to-all ----
    _applyAllCheck = new QCheckBox(
        tr("Apply this to all remaining conflicts in this operation"), this);

    // ---- action buttons ----
    auto* skipBtn      = new QPushButton(tr("Skip"), this);
    auto* renameBtn    = new QPushButton(tr("Rename"), this);
    auto* overwriteBtn = new QPushButton(tr("Overwrite"), this);
    skipBtn->setToolTip(tr("Leave the existing file untouched"));
    renameBtn->setToolTip(tr("Keep both — the incoming file gets a new name"));
    overwriteBtn->setToolTip(tr("Replace the existing file"));

    // Skip is the safe default (Enter) and Escape falls through to it too.
    skipBtn->setDefault(true);
    connect(skipBtn,      &QPushButton::clicked, this, [this]() { chooseAndAccept(Task::Skip); });
    connect(renameBtn,    &QPushButton::clicked, this, [this]() { chooseAndAccept(Task::Rename); });
    connect(overwriteBtn, &QPushButton::clicked, this, [this]() { chooseAndAccept(Task::Overwrite); });

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    buttonRow->addWidget(skipBtn);
    buttonRow->addWidget(renameBtn);
    buttonRow->addWidget(overwriteBtn);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->addLayout(top);
    layout->addWidget(_applyAllCheck);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    layout->addLayout(buttonRow);
}

void ConflictDialog::chooseAndAccept(Task::ConflictAnswer answer) {
    _answer = answer;
    _applyToAll = _applyAllCheck && _applyAllCheck->isChecked();
    accept();
}
