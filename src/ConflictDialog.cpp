#include "ConflictDialog.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "ThumbnailCache.h"

namespace {
// Preview box edge in px. Two of these side by side stay well within the
// 1280×720 budget (~500px wide dialog).
constexpr int kPreviewPx = 180;
}  // namespace

ConflictDialog::ConflictDialog(int kind, const QVariantMap& context,
                               ThumbnailCache* thumbs, QWidget* parent)
    : QDialog(parent), _thumbs(thumbs) {
    // Application-modal: the whole app blocks until the user answers, which is
    // the point — the worker thread is parked on a wait condition and the dock
    // controls (pause / stop) would race an answer if they stayed live.
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(tr("File already exists"));

    const QString src = context.value("src").toString();
    const QString dst = context.value("dst").toString();
    const QString dstName = QFileInfo(dst).fileName();

    // ---- header: warning icon + primary message ----
    auto* top = new QHBoxLayout();
    top->setSpacing(12);

    auto* icon = new QLabel(this);
    const int px = style()->pixelMetric(QStyle::PM_MessageBoxIconSize);
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(px, px));
    icon->setAlignment(Qt::AlignTop);
    top->addWidget(icon, 0);

    const QString primary = (kind == Task::DestinationExists && !dstName.isEmpty())
        ? tr("\"%1\" already exists in the destination folder.").arg(dstName)
        : tr("The destination already exists.");
    auto* primaryLabel = new QLabel(primary, this);
    primaryLabel->setWordWrap(true);
    QFont f = primaryLabel->font();
    f.setBold(true);
    primaryLabel->setFont(f);
    top->addWidget(primaryLabel, 1);

    // ---- side-by-side previews: Existing (dst) vs Incoming (src) ----
    auto* panes = new QHBoxLayout();
    panes->setSpacing(16);
    panes->addStretch(1);
    panes->addWidget(buildPane(tr("Existing"), dst));
    auto* arrow = new QLabel(QStringLiteral("→"), this);
    arrow->setAlignment(Qt::AlignCenter);
    panes->addWidget(arrow, 0);
    panes->addWidget(buildPane(tr("Incoming"), src));
    panes->addStretch(1);

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
    layout->addLayout(panes);
    layout->addWidget(_applyAllCheck);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    layout->addLayout(buttonRow);
}

ConflictDialog::~ConflictDialog() {
    if (_thumbs)
        for (const QString& p : _subscribed) _thumbs->unsubscribe(p);
}

QWidget* ConflictDialog::buildPane(const QString& title, const QString& path) {
    auto* pane = new QWidget(this);
    auto* col = new QVBoxLayout(pane);
    col->setSpacing(4);
    col->setContentsMargins(0, 0, 0, 0);

    auto* titleLabel = new QLabel(title, pane);
    titleLabel->setAlignment(Qt::AlignHCenter);
    QFont tf = titleLabel->font();
    tf.setBold(true);
    titleLabel->setFont(tf);
    col->addWidget(titleLabel);

    auto* image = new QLabel(pane);
    image->setObjectName("conflictPreview");
    image->setFixedSize(kPreviewPx, kPreviewPx);
    image->setAlignment(Qt::AlignCenter);
    image->setFrameShape(QFrame::StyledPanel);
    // Placeholder: the standard file icon until (and unless) a thumbnail lands.
    image->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(kPreviewPx / 2,
                                                                       kPreviewPx / 2));
    col->addWidget(image, 0, Qt::AlignHCenter);

    // Caption: "W × H · size" then the modified date. Both are header-only /
    // stat reads (QImageReader::size(), QFileInfo) — cheap even on a share.
    const QFileInfo fi(path);
    QImageReader reader(path);
    const QSize dim = reader.size();  // invalid for non-images
    QStringList spec;
    if (dim.isValid())
        spec << tr("%1 × %2").arg(dim.width()).arg(dim.height());
    if (fi.size() > 0)
        spec << QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat);

    auto* meta = new QLabel(pane);
    meta->setObjectName("conflictMeta");
    meta->setAlignment(Qt::AlignHCenter);
    meta->setTextFormat(Qt::PlainText);
    QStringList lines;
    if (!spec.isEmpty()) lines << spec.join(QStringLiteral(" · "));
    if (fi.lastModified().isValid())
        lines << QLocale().toString(fi.lastModified(), QLocale::ShortFormat);
    meta->setText(lines.join(QLatin1Char('\n')));
    col->addWidget(meta);

    // Async thumbnail for images only, reusing the shared cache. Results arrive
    // via thumbnailReady during exec()'s nested loop. mtime uses the same
    // toSecsSinceEpoch() convention as FileListView so a still-valid cached
    // row is a hit rather than a needless regeneration.
    if (_thumbs && dim.isValid() && !_panes.contains(path)) {
        if (_subscribed.isEmpty())  // connect once, on the first subscription
            connect(_thumbs, &ThumbnailCache::thumbnailReady,
                    this, &ConflictDialog::onThumbnailReady);
        _panes.insert(path, image);
        _subscribed << path;
        _thumbs->subscribe(path, fi.lastModified().toSecsSinceEpoch(), fi.size(), 0);
    }
    return pane;
}

void ConflictDialog::onThumbnailReady(const QString& path, const QImage& image) {
    QLabel* label = _panes.value(path);
    if (!label || image.isNull()) return;
    label->setPixmap(QPixmap::fromImage(image).scaled(
        label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ConflictDialog::chooseAndAccept(Task::ConflictAnswer answer) {
    _answer = answer;
    _applyToAll = _applyAllCheck && _applyAllCheck->isChecked();
    accept();
}
