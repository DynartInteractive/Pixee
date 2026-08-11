#include "MetadataPanel.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Add a row only when the value is non-empty, so absent tags don't clutter
// the summary. Returns whether a row was added.
bool addIf(QTreeWidgetItem* group, const QString& key, const QString& value) {
    if (value.isEmpty()) return false;
    auto* row = new QTreeWidgetItem(group);
    row->setText(0, key);
    row->setText(1, value);
    row->setToolTip(1, value);
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

    clearMetadata();
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
