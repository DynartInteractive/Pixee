#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include "ImageAdjust.h"

class QCheckBox;
class QResizeEvent;
class QSpinBox;
class QTimer;

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

    // Dirty-state plumbing for File → Save. The editing ops (Crop / Flip /
    // Rotate) call setModified(true) after mutating the in-memory buffer;
    // setImage()/setPlaceholder()/clear() reset it to false on image change.
    // modifiedChanged() lets MainWindow re-evaluate the Save action's state.
    // True when anything is unsaved: a baked geometry edit, a pending colour
    // adjustment, or both.
    bool isModified() const;
    void setModified(bool on);

    // The image the user is currently looking at, including every pending
    // edit. This is what File → Save / Save As write out. Baked edits are
    // already in the buffer; the colour adjustment is applied here, at full
    // resolution, which is the one deliberately synchronous cost in the
    // pipeline - it happens once, when the user asks to save.
    QImage editedImage() const;

    // Pixel size of the image as the user currently sees it. Deliberately
    // separate from editedImage(): a colour adjustment never changes the size,
    // so callers that only want dimensions must not pay for the full-resolution
    // adjustment pass.
    QSize currentSize() const { return currentImage().size(); }

    // -- Live colour adjustment ---------------------------------------------
    // Unlike rotate / flip / crop these are *parameters*, not pixels: they are
    // re-applied to the pristine source every time they change, so dragging a
    // slider never compounds and returning it to zero restores the original
    // exactly. See the note at the top of ImageAdjust.h.
    const ImageAdjust::Adjustments& adjustments() const { return _adjust; }
    void setAdjustments(const ImageAdjust::Adjustments& adjustments);
    void resetAdjustments();
    bool hasAdjustments() const { return !_adjust.isIdentity(); }

    // Keep a preview-resolution copy of the displayed pixels available (and
    // previewUpdated flowing) even when no adjustment is pending. Off by
    // default so plain browsing pays nothing; the histogram dock turns it on
    // while it is visible.
    void setPreviewEnabled(bool on);
    bool previewEnabled() const { return _previewEnabled; }
    const QImage& previewImage() const { return _preview; }

    void zoomIn();
    void zoomOut();

    // In-viewer editing ops. Each bakes the transform into an in-memory buffer
    // (_edited) on top of the loaded image and marks the viewer modified. They
    // no-op while only a thumbnail placeholder is showing (the full-res load
    // would overwrite the edit) or when there's no image. Rotation re-encodes
    // pixels — the lossless EXIF-orientation path needs Exiv2 *write*, which
    // the metadata backend doesn't have yet.
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();

    // Crop: beginCrop() enters an interactive mode where a rubber-band drag
    // defines the crop rectangle; Enter (or a second click-release with a
    // valid rect) applies it, Esc cancels. Pan/zoom are suppressed while
    // cropping so the widget↔image mapping stays a simple affine. inCropMode()
    // reflects the state; cropModeChanged() lets MainWindow show a hint.
    void beginCrop();
    bool inCropMode() const { return _cropMode; }

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
    // Emitted when the dirty flag flips (see isModified()).
    void modifiedChanged(bool modified);
    // Emitted when crop mode is entered (true) or left (false).
    void cropModeChanged(bool active);
    // Emitted after an edit bakes into the buffer, carrying the new pixel size
    // (rotate/flip/crop can all change dimensions) so the status bar can update.
    void imageEdited(QSize size);
    // Emitted whenever the adjustment parameters change (including a reset).
    void adjustmentsChanged(ImageAdjust::Adjustments adjustments);
    // The displayed pixels, at preview resolution, after every change that
    // alters them. This is the seam the histogram panel hangs off: one
    // producer, so the graph can never disagree with the image above it.
    void previewUpdated(QImage preview);

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
    void resizeEvent(QResizeEvent* event) override;
    // Watches the crop tool bar so Enter / Esc still apply / cancel while one
    // of its spin boxes holds the keyboard focus.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // What the pointer is over on the crop marquee: one of the eight drag
    // handles (clockwise from the top-left), Body for the area inside them
    // (drag to move the whole selection), or None for anywhere else.
    enum class CropHandle {
        None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft,
        Left, Body
    };

    QSize currentDrawSize() const;
    // The logical image: the source with any baked geometry edit applied, but
    // *not* the colour adjustment. Everything that measures or transforms the
    // image works from this - adjustments are per-pixel, so they neither
    // change its size nor need re-doing when it is rotated or cropped.
    const QImage& currentImage() const;
    // The pixels to actually paint: the adjusted preview when one is live,
    // otherwise the logical image itself.
    const QImage& displayImage() const;
    // Rebuild the proxy and its adjusted result, then emit previewUpdated.
    // Drop the parameters on an image change and tell subscribers, so the
    // Adjust panel's sliders follow the viewer instead of showing the previous
    // image's settings over the new one.
    void clearAdjustments();
    void refreshPreview();
    // Drop the cached proxy (the logical pixels changed under it) and rebuild.
    void invalidatePreview();
    // Top up the proxy when the view grew since it was built. Called from
    // paintEvent, so deliberately silent - a resolution bump is not a change
    // of content, and emitting a signal mid-paint invites re-entrancy.
    void ensurePreviewResolution();
    // Width the proxy needs, quantised so a resize drag does not rescale the
    // source on every pixel of movement.
    int previewTargetWidth() const;
    // The on-screen rectangle the image currently occupies (top-left + draw
    // size, honouring pan). Empty when there's no image. Shared by paintEvent
    // and the crop coordinate mapping so they never disagree.
    QRect imageRectOnWidget() const;
    // Map a widget-space rectangle to image pixels, clamped to the image.
    QRect mapWidgetRectToImage(const QRect& widgetRect) const;
    // Replace the edited buffer with `img`, mark modified, reset pan, repaint.
    void commitEdit(const QImage& img);
    void cancelCrop();
    void applyCrop();
    // Tear down crop-mode state (marquee, ants timer, tool bar) without
    // touching the image. Shared by cancelCrop and applyCrop.
    void endCropMode();
    void paintCropOverlay(class QPainter& p, const QRect& imageRect);
    void paintCropHandles(class QPainter& p, const QRect& sel);
    void paintCropReadout(class QPainter& p);
    // The image-space rect the current marquee would cut, snapped to the exact
    // locked ratio. Empty when the marquee is too small to be a crop. Shared by
    // applyCrop and the readout so the number shown is the number produced.
    QRect cropRectInImage() const;
    // Widget-space square of one handle on `sel`.
    QRect handleRect(CropHandle h, const QRect& sel) const;
    // Which handle sits under `pos`, using a grab box larger than the painted
    // square. Corners win over edges where the two overlap on a small marquee.
    CropHandle handleAt(const QPoint& pos) const;
    // The corner that stays put while `h` is dragged.
    QPoint anchorFor(CropHandle h, const QRect& sel) const;
    // Build a marquee from a fixed `anchor` corner and the dragged `moving`
    // point, honouring the locked ratio and the image bounds.
    QRect rectFromCorner(const QPoint& anchor, const QPoint& moving) const;
    // Move one edge of the marquee. Under a locked ratio the perpendicular
    // dimension follows, growing symmetrically about the rect's centre.
    QRect rectFromEdge(CropHandle h, const QPoint& moving) const;
    // Slide the whole marquee so the grabbed point tracks `moving`, kept
    // wholly inside the image.
    QRect rectMoved(const QPoint& moving) const;
    bool ratioLocked() const;
    double lockedRatio() const;   // width / height, or 0 when not locked
    // Re-derive the marquee after the ratio controls changed.
    void reapplyRatio();
    void buildCropBar();
    void positionCropBar();
    void setCropCursor(CropHandle h);
    void clampTranslate();
    void updateCursor();
    // Pan starts the moment any pan-trigger becomes active (Space in
    // NoFit mode, or middle-button held) and ends when none are. The
    // anchor is the mouse position at the moment the trigger first
    // engages, so subsequent mouseMoveEvents drag relative to it.
    bool wantPan() const;
    void beginPanIfNeeded(const QPoint& mousePos);
    void endPanIfDone();

    QImage _image;               // the loaded image (pristine, as decoded)
    QImage _baked;               // geometry edits baked in; null == none pending
    bool _modified = false;      // unsaved *baked* edit pending (see isModified())
    bool _placeholder = false;   // showing a thumbnail placeholder, not full-res

    // Colour-adjustment layer, sitting on top of _baked. _previewSource is the
    // logical image downscaled to roughly what the screen shows, so a slider
    // drag re-adjusts ~1-2 MP instead of the full 24+; _preview is that proxy
    // with _adjust applied, and is what paintEvent draws. Both are dropped
    // whenever the logical pixels change.
    ImageAdjust::Adjustments _adjust;
    QImage _previewSource;
    QImage _preview;
    bool _previewEnabled = false;  // keep a preview alive even with no adjustment
    bool _bypassAdjust = false;    // B held: show the unadjusted image
    // Crop-mode state. _cropRect is in widget coordinates (normalized on use).
    bool _cropMode = false;
    bool _cropDragging = false;
    QRect _cropRect;
    // Which handle the live drag grabbed (None == dragging a fresh marquee),
    // and the widget-space point that stays fixed while it moves.
    CropHandle _cropHandle = CropHandle::None;
    QPoint _cropAnchor;
    // Grab point relative to the marquee's top-left, for a Body drag.
    QPoint _cropMoveOffset;
    // Marching ants: a dash offset stepped by _antsTimer. The timer runs only
    // while a marquee is up, so an idle viewer repaints nothing.
    int _antsPhase = 0;
    QTimer* _antsTimer = nullptr;
    // Crop tool bar. A child overlay rather than a MainWindow QToolBar so it
    // travels with the image into F11 fullscreen and costs the browser layout
    // nothing. Built lazily on first beginCrop(), shown only in crop mode.
    QWidget* _cropBar = nullptr;
    QCheckBox* _ratioCheck = nullptr;
    QSpinBox* _ratioW = nullptr;
    QSpinBox* _ratioH = nullptr;
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
