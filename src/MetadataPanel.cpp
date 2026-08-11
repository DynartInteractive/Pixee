#include "MetadataPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// The full (untruncated) value of a row, for the clipboard.
constexpr int kFullValueRole = Qt::UserRole;

// Single-line, length-capped preview for a cell — embedded text (ComfyUI
// prompt/workflow JSON) can be tens of KB and multi-line, which the tree can't
// show sanely. The full value stays available via kFullValueRole for copying.
QString previewValue(const QString& v) {
    QString s = v;
    s.replace(QLatin1Char('\n'), QLatin1Char(' '))
     .replace(QLatin1Char('\r'), QLatin1Char(' '))
     .replace(QLatin1Char('\t'), QLatin1Char(' '));
    if (s.size() > 200) s = s.left(200) + QStringLiteral("…");
    return s;
}

// Add a row only when the value is non-empty, so absent tags don't clutter
// the summary. Stores the full value for copying; only short values get a
// full tooltip (a multi-KB tooltip is useless). Returns whether a row was added.
bool addIf(QTreeWidgetItem* group, const QString& key, const QString& value) {
    if (value.isEmpty()) return false;
    auto* row = new QTreeWidgetItem(group);
    row->setText(0, key);
    row->setText(1, previewValue(value));
    row->setData(1, kFullValueRole, value);
    if (value.size() <= 400 && !value.contains(QLatin1Char('\n')))
        row->setToolTip(1, value);
    else
        row->setToolTip(1, QObject::tr("Long value — right-click or Ctrl+C to copy"));
    return true;
}

QString humanSize(qint64 bytes) {
    if (bytes <= 0) return QString();
    return QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeTraditionalFormat);
}

// EXIF Orientation 1..8 → short human description (the panel shows the raw
// value too via the number). 0/unknown yields empty.
QString orientationText(int o) {
    switch (o) {
        case 1: return QObject::tr("Normal");
        case 2: return QObject::tr("Mirrored");
        case 3: return QObject::tr("Rotated 180°");
        case 4: return QObject::tr("Mirrored, 180°");
        case 5: return QObject::tr("Mirrored, 90° CCW");
        case 6: return QObject::tr("Rotated 90° CW");
        case 7: return QObject::tr("Mirrored, 90° CW");
        case 8: return QObject::tr("Rotated 90° CCW");
        default: return QString();
    }
}

}  // namespace

MetadataPanel::MetadataPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("metadataPanel");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    _header = new QLabel(this);
    _header->setObjectName("metadataHeader");
    _header->setWordWrap(true);
    _header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_header);

    _tree = new QTreeWidget(this);
    _tree->setObjectName("metadataTree");
    _tree->setColumnCount(2);
    _tree->setHeaderLabels({tr("Property"), tr("Value")});
    _tree->setRootIsDecorated(true);
    _tree->setUniformRowHeights(true);
    _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tree->setTextElideMode(Qt::ElideRight);
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    _tree->header()->setStretchLastSection(true);
    layout->addWidget(_tree, 1);

    // Copy affordances — right-click menu and Ctrl+C both lift the selected
    // rows' *full* values (the ComfyUI prompt/workflow blobs, EXIF values, …).
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_tree, &QTreeWidget::customContextMenuRequested,
            this, &MetadataPanel::showTreeContextMenu);
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, _tree);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this] { copySelected(false); });

    clearMetadata();
}

void MetadataPanel::copySelected(bool withKey) {
    const QList<QTreeWidgetItem*> sel = _tree->selectedItems();
    QStringList lines;
    for (QTreeWidgetItem* it : sel) {
        // Skip group header rows (they span the first column, no value).
        if (it->data(1, kFullValueRole).isNull() && it->text(1).isEmpty()) continue;
        const QString full = it->data(1, kFullValueRole).toString();
        const QString value = full.isEmpty() ? it->text(1) : full;
        lines << (withKey ? (it->text(0) + QLatin1Char('\t') + value) : value);
    }
    if (!lines.isEmpty())
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void MetadataPanel::showTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = _tree->itemAt(pos);
    if (!item) return;
    QMenu menu(this);
    QAction* copyVal = menu.addAction(tr("Copy value"));
    QAction* copyPair = menu.addAction(tr("Copy name = value"));
    QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
    if (chosen == copyVal) copySelected(false);
    else if (chosen == copyPair) copySelected(true);
}

QTreeWidgetItem* MetadataPanel::addGroup(const QString& title) {
    auto* group = new QTreeWidgetItem(_tree);
    group->setText(0, title);
    group->setFirstColumnSpanned(true);
    group->setExpanded(true);
    // Non-selectable header row.
    group->setFlags(Qt::ItemIsEnabled);
    return group;
}

void MetadataPanel::addRow(QTreeWidgetItem* group, const QString& key,
                           const QString& value) {
    addIf(group, key, value);
}

void MetadataPanel::showLoading(const QString& path) {
    _tree->clear();
    _header->setText(tr("%1\nReading metadata…")
                         .arg(QFileInfo(path).fileName()));
}

void MetadataPanel::clearMetadata() {
    _tree->clear();
    _header->setText(tr("No image selected"));
}

void MetadataPanel::setMetadata(const ImageMetadata& md) {
    _tree->clear();
    _header->setText(QFileInfo(md.path).fileName());

    // --- File (always) ----------------------------------------------------
    QTreeWidgetItem* file = addGroup(tr("File"));
    if (md.pixelSize.isValid()) {
        addRow(file, tr("Dimensions"),
               tr("%1 × %2").arg(md.pixelSize.width()).arg(md.pixelSize.height()));
        addRow(file, tr("Megapixels"),
               QString::number(md.pixelSize.width() * double(md.pixelSize.height())
                                   / 1'000'000.0, 'f', 1));
    }
    addRow(file, tr("Format"), md.format);
    addRow(file, tr("File size"), humanSize(md.fileBytes));
    if (md.fileModified.isValid())
        addRow(file, tr("Modified"),
               QLocale().toString(md.fileModified, QLocale::ShortFormat));

    // --- Rich groups (Exiv2 build only) -----------------------------------
    QTreeWidgetItem* camera = addGroup(tr("Camera"));
    addRow(camera, tr("Make"), md.cameraMake);
    addRow(camera, tr("Model"), md.cameraModel);
    addRow(camera, tr("Lens"), md.lens);
    addRow(camera, tr("Date taken"), md.dateTaken);
    if (camera->childCount() == 0) delete camera;

    QTreeWidgetItem* exposure = addGroup(tr("Exposure"));
    addRow(exposure, tr("Aperture"), md.fNumber);
    addRow(exposure, tr("Shutter"), md.exposureTime);
    addRow(exposure, tr("ISO"), md.isoSpeed);
    addRow(exposure, tr("Focal length"), md.focalLength);
    if (md.orientation > 0) {
        const QString ot = orientationText(md.orientation);
        addRow(exposure, tr("Orientation"),
               ot.isEmpty() ? QString::number(md.orientation)
                            : tr("%1 (%2)").arg(md.orientation).arg(ot));
    }
    if (exposure->childCount() == 0) delete exposure;

    if (md.hasGps) {
        QTreeWidgetItem* loc = addGroup(tr("Location"));
        addRow(loc, tr("Coordinates"), md.gpsText);
    }

    // --- Embedded text chunks (PNG tEXt/etc.) -----------------------------
    // AI-generation data lives here: ComfyUI `prompt`/`workflow`, A1111
    // `parameters`, plus standard Description/Software/Comment.
    if (!md.textChunks.isEmpty()) {
        QTreeWidgetItem* text = addGroup(tr("Embedded text (%1)").arg(md.textChunks.size()));
        for (const auto& kv : md.textChunks)
            addRow(text, kv.first, kv.second);
    }

    // --- Full dump (power users) ------------------------------------------
    if (!md.allTags.isEmpty()) {
        QTreeWidgetItem* all = addGroup(tr("All metadata (%1)").arg(md.allTags.size()));
        all->setExpanded(false);  // collapsed by default — it's long
        for (const auto& kv : md.allTags)
            addRow(all, kv.first, kv.second);
    }

    // Hint when the build has no Exiv2 so an empty summary isn't mistaken
    // for "this file has no metadata".
    if (!md.exiv2Available) {
        QTreeWidgetItem* note = addGroup(tr("Note"));
        note->setText(0, tr("EXIF/IPTC/XMP not available in this build"));
    }
}
