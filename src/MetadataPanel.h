#ifndef METADATAPANEL_H
#define METADATAPANEL_H

#include <QString>
#include <QWidget>

#include "ImageMetadata.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

// Read-only info panel for the currently focused image. Lives inside the
// right-side "Metadata" dock. Pure Qt — no Exiv2 dependency; it just renders
// whatever ImageMetadata the (off-thread) MetadataReader produced.
//
// A two-column tree (Property / Value) grouped into collapsible sections:
// File (always), then Camera / Exposure / Location and a full "All metadata"
// dump when the build carries Exiv2 and the file has that data.
class MetadataPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget* parent = nullptr);

public slots:
    // Render the given metadata.
    void setMetadata(const ImageMetadata& md);
    // Show a "Loading…" hint for a path whose read is in flight.
    void showLoading(const QString& path);
    // Empty state (no image focused).
    void clearMetadata();

private slots:
    void showTreeContextMenu(const QPoint& pos);

private:
    QTreeWidgetItem* addGroup(const QString& title);
    void addRow(QTreeWidgetItem* group, const QString& key, const QString& value);
    // Copy the selected rows to the clipboard. withKey=false copies just the
    // values (each row's full value, not the elided cell text — important for
    // long AI-generation blobs); withKey=true copies "Key\tValue" lines.
    void copySelected(bool withKey);

    QLabel*      _header;   // file name / status line above the tree
    QTreeWidget* _tree;
};

#endif // METADATAPANEL_H
