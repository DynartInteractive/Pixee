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
//   *                          : 100% (also switches out of any fit mode)
//   /                          : Fit-to-window, large only
//   Space + Left-drag          : pan
//   Middle-drag                : pan
class ViewerWidget : public QWidget
{
    Q_OBJECT
public:
    enum class FitMode {
        NoFit,         // honour the explicit zoom from kZoomLevels[_zoomIndex]
        Fit,           // scale image to widget, up or down
        FitLargeOnly,  // scale down only when image > widget; else native size
    };

    explicit ViewerWidget(QWidget* parent = nullptr);

    // Replaces the image. When lockZoom() is false (default), resets fit
    // mode + zoom + pan to defaults; when true, preserves the user's
    // current fit / zoom / pan so they survive prev/next navigation.
    // Rotation always resets — it's a per-image transform, not a view
    // setting.
    void setImage(const QImage& image);
    // Like setImage, but forces FitMode::Fit so the small thumbnail scales
    // up to fill the viewport — gives the user a (blurry) full-size preview
    // while the real image streams in. updateImage doesn't reset fit mode,
    // so when the full-res replaces the placeholder it stays in Fit (which
    // is identical to FitLargeOnly for any image bigger than the viewport).
    void setPlaceholder(const QImage& image);
    // Replaces the image without touching fit / zoom / pan — used when the
    // full-res arrives to swap out the placeholder thumbnail under the
    // user's existing zoom state.
    void updateImage(const QImage& image);
    void clear();
    bool hasImage() const { return !_image.isNull(); }

    void zoomIn();
    void zoomOut();
    void rotateLeft();
    void rotateRight();

    // Zoom-menu API. setZoomPercent flips fit mode to NoFit and snaps to
    // the matching kZoomLevels entry; currentZoomPercent returns 0 when
    // any fit mode is active (i.e. no specific percent applies).
    FitMode fitMode() const { return _fitMode; }
    void setFitMode(FitMode mode);
    int  currentZoomPercent() const;
    void setZoomPercent(int pct);
    bool lockZoom() const { return _lockZoom; }
    void setLockZoom(bool on) { _lockZoom = on; }

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
    void focusOutEvent(QFocusEvent* event) override;

private:
    QSize currentDrawSize() const;
    const QImage& currentImage() const;
    void invalidateRotation();
    void clampTranslate();
    void updateCursor();
    // Pan starts the moment any pan-trigger becomes active (Space in
    // NoFit mode, or middle-button held) and ends when none are. The
    // anchor is the mouse position at the moment the trigger first
    // engages, so subsequent mouseMoveEvents drag relative to it.
    bool wantPan() const;
    void beginPanIfNeeded(const QPoint& mousePos);
    void endPanIfDone();

    QImage _image;
    QImage _rotatedImage;        // cached _image rotated by _rotation, when != 0
    int _rotation = 0;           // 0/90/180/270; per-image, resets on setImage
    FitMode _fitMode = FitMode::FitLargeOnly;
    bool _lockZoom = false;      // when true, fit / zoom / pan survive setImage
    int _zoomIndex = 0;          // index into kZoomLevels (used when _fitMode == NoFit)
    QPoint _translate;           // pan offset relative to widget center
    bool _spaceDown = false;     // Space held → pan with mouse motion (Photoshop-style)
    bool _midDown = false;       // middle button held → also pans
    bool _panning = false;       // dragging right now
    QPoint _panStart;            // mouse-pos − translate at drag start
};

#endif // VIEWERWIDGET_H
