#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QImage>
#include <QPoint>
#include <QWidget>

// Image viewer surface. Phase 3 adds pan + zoom on top of Phase 1's
// fit-to-window paint and Phase 2's prev/next signals.
//
// Controls:
//   Esc / Enter / double-click : dismiss
//   Left / Right               : prev / next image
//   Wheel                      : prev / next image
//   Ctrl + Wheel               : zoom in / out at the cursor
//   + / =                      : zoom in
//   -                          : zoom out
//   0 / *                      : toggle fit-to-window
//   1                          : 1:1 (actual size)
//   Space + Left-drag          : pan
//   Middle-drag                : pan
class ViewerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ViewerWidget(QWidget* parent = nullptr);

    // Replaces the image and resets fit / zoom / pan to defaults. Use when
    // switching to a new image (placeholder or new prev/next selection).
    void setImage(const QImage& image);
    // Replaces the image without touching fit / zoom / pan — used when the
    // full-res arrives to swap out the placeholder thumbnail under the
    // user's existing zoom state.
    void updateImage(const QImage& image);
    void clear();
    bool hasImage() const { return !_image.isNull(); }

    void zoomIn();
    void zoomOut();
    void toggleFit();
    void actualSize();
    void rotateLeft();
    void rotateRight();

signals:
    void dismissed();
    void prevRequested();
    void nextRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QSize currentDrawSize() const;
    const QImage& currentImage() const;
    void invalidateRotation();
    void clampTranslate();
    void updateCursor();

    QImage _image;
    QImage _rotatedImage;        // cached _image rotated by _rotation, when != 0
    int _rotation = 0;           // 0/90/180/270; per-image, resets on setImage
    bool _fit = true;
    int _zoomIndex = 0;          // index into kZoomLevels (only when !_fit)
    QPoint _translate;           // pan offset relative to widget center
    bool _spaceDown = false;     // Space held → ready to pan with LMB
    bool _panning = false;       // dragging right now (LMB-with-Space or MMB)
    QPoint _panStart;            // mouse-pos − translate at drag start
};

#endif // VIEWERWIDGET_H
