#include "ViewerWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QTransform>
#include <QWheelEvent>

namespace {
constexpr double kZoomLevels[] = {
    0.10, 0.25, 0.50, 0.75, 1.0, 1.25, 1.50, 2.0, 4.0, 6.0, 8.0, 12.0, 16.0
};
constexpr int kZoomCount = static_cast<int>(sizeof(kZoomLevels) / sizeof(double));
constexpr int kZoomIndex100 = 4;  // index of 1.0 above

int percentForIndex(int i) {
    return int(kZoomLevels[i] * 100.0 + 0.5);
}
}

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(1, 1);
    setAutoFillBackground(false);
    // Tracking is on so mouseMoveEvents fire even with no button held —
    // needed for Space-only panning (cursor moves drag the image).
    setMouseTracking(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    _zoomIndex = kZoomIndex100;
}

void ViewerWidget::setModified(bool on) {
    if (_modified == on) return;
    _modified = on;
    emit modifiedChanged(_modified);
}

void ViewerWidget::setImage(const QImage& image) {
    _image = image;
    _placeholder = false;
    // A new image starts clean; any pending edit belonged to the old one.
    _edited = QImage();
    setModified(false);
    cancelCrop();
    if (!_lockZoom) {
        // Fresh-image defaults: FitLargeOnly + 100% baseline + centered.
        // (When locked, keep the user's current fit mode, zoom, and pan;
        // clampTranslate will re-fit the pan to the new image's bounds
        // on the first paint.)
        _fitMode = FitMode::FitLargeOnly;
        _zoomIndex = kZoomIndex100;
        _translate = QPoint();
    }
    update();
}

void ViewerWidget::setPlaceholder(const QImage& image) {
    _image = image;
    _placeholder = true;
    _edited = QImage();
    setModified(false);
    cancelCrop();
    if (!_lockZoom) {
        _fitMode = FitMode::Fit;
        _zoomIndex = kZoomIndex100;
        _translate = QPoint();
    }
    update();
}

void ViewerWidget::updateImage(const QImage& image) {
    _image = image;
    _placeholder = false;
    update();
}

void ViewerWidget::clear() {
    _image = QImage();
    _edited = QImage();
    _placeholder = false;
    setModified(false);
    cancelCrop();
    _translate = QPoint();
    update();
}

const QImage& ViewerWidget::currentImage() const {
    return _edited.isNull() ? _image : _edited;
}

void ViewerWidget::commitEdit(const QImage& img) {
    if (img.isNull()) return;
    _edited = img;
    setModified(true);
    _translate = QPoint();   // aspect / bounds changed; recenter
    update();
    emit imageEdited(img.size());
}

void ViewerWidget::rotateLeft() {
    if (_placeholder || currentImage().isNull()) return;
    QTransform xform;
    xform.rotate(-90);
    commitEdit(currentImage().transformed(xform, Qt::SmoothTransformation));
}

void ViewerWidget::rotateRight() {
    if (_placeholder || currentImage().isNull()) return;
    QTransform xform;
    xform.rotate(90);
    commitEdit(currentImage().transformed(xform, Qt::SmoothTransformation));
}

void ViewerWidget::flipHorizontal() {
    if (_placeholder || currentImage().isNull()) return;
    commitEdit(currentImage().mirrored(/*horizontal=*/true, /*vertical=*/false));
}

void ViewerWidget::flipVertical() {
    if (_placeholder || currentImage().isNull()) return;
    commitEdit(currentImage().mirrored(/*horizontal=*/false, /*vertical=*/true));
}

void ViewerWidget::beginCrop() {
    if (_placeholder || currentImage().isNull() || _cropMode) return;
    _cropMode = true;
    _cropDragging = false;
    _cropRect = QRect();
    // Pan makes no sense mid-crop; drop any drag state and switch the cursor.
    _spaceDown = _midDown = _panning = false;
    setCursor(Qt::CrossCursor);
    update();
    emit cropModeChanged(true);
}

void ViewerWidget::cancelCrop() {
    if (!_cropMode) return;
    _cropMode = false;
    _cropDragging = false;
    _cropRect = QRect();
    updateCursor();
    update();
    emit cropModeChanged(false);
}

void ViewerWidget::applyCrop() {
    if (!_cropMode) return;
    const QRect sel = _cropRect.normalized().intersected(imageRectOnWidget());
    // Too small a selection is treated as "no crop" — just leave crop mode.
    if (sel.width() < 4 || sel.height() < 4) {
        cancelCrop();
        return;
    }
    const QRect imgRect = mapWidgetRectToImage(sel);
    if (imgRect.width() < 1 || imgRect.height() < 1) {
        cancelCrop();
        return;
    }
    const QImage cropped = currentImage().copy(imgRect);
    // Leave crop mode first so commitEdit's repaint doesn't draw the overlay.
    _cropMode = false;
    _cropDragging = false;
    _cropRect = QRect();
    updateCursor();
    commitEdit(cropped);          // bakes the crop, marks modified, repaints
    emit cropModeChanged(false);  // after commit so listeners see the new size
}

QRect ViewerWidget::imageRectOnWidget() const {
    const QImage& img = currentImage();
    if (img.isNull()) return QRect();
    const QSize ds = currentDrawSize();
    if (ds.isEmpty()) return QRect();
    const QPoint center(width() / 2, height() / 2);
    const QPoint topLeft(
        center.x() - ds.width()  / 2 + _translate.x(),
        center.y() - ds.height() / 2 + _translate.y());
    return QRect(topLeft, ds);
}

QRect ViewerWidget::mapWidgetRectToImage(const QRect& widgetRect) const {
    const QRect dst = imageRectOnWidget();
    const QImage& img = currentImage();
    if (dst.isEmpty() || img.isNull()) return QRect();
    const double sx = double(img.width())  / dst.width();
    const double sy = double(img.height()) / dst.height();
    const int x = qRound((widgetRect.x() - dst.x()) * sx);
    const int y = qRound((widgetRect.y() - dst.y()) * sy);
    const int w = qRound(widgetRect.width()  * sx);
    const int h = qRound(widgetRect.height() * sy);
    return QRect(x, y, w, h).intersected(img.rect());
}

QSize ViewerWidget::currentDrawSize() const {
    const QImage& img = currentImage();
    if (img.isNull()) return QSize();
    switch (_fitMode) {
    case FitMode::Fit:
        return img.size().scaled(size(), Qt::KeepAspectRatio);
    case FitMode::FitLargeOnly:
        // Only scale down — small images stay at native size so a
        // 32×32 icon doesn't blow up to fill the viewport.
        if (img.width() <= width() && img.height() <= height()) {
            return img.size();
        }
        return img.size().scaled(size(), Qt::KeepAspectRatio);
    case FitMode::NoFit: {
        const double z = kZoomLevels[_zoomIndex];
        return QSize(int(img.width() * z), int(img.height() * z));
    }
    }
    return img.size();
}

void ViewerWidget::clampTranslate() {
    if (currentImage().isNull()) return;
    const QSize ds = currentDrawSize();
    // Allow pan up to "image edge meets widget edge" — never produces
    // background gutter inside the image's reach.
    const int maxX = qMax(0, (ds.width()  - width())  / 2);
    const int maxY = qMax(0, (ds.height() - height()) / 2);
    _translate.setX(qBound(-maxX, _translate.x(), maxX));
    _translate.setY(qBound(-maxY, _translate.y(), maxY));
}

void ViewerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 24));
    const QImage& img = currentImage();
    if (img.isNull()) return;

    clampTranslate();
    const QRect dst = imageRectOnWidget();
    if (dst.isEmpty()) return;

    // Smooth for downscale (any fit mode + NoFit < 1.0); nearest for
    // upscale in NoFit (>1.0) so pixel art stays crisp when zooming
    // above 1:1 — same convention as the thumbnail upscale path.
    const bool smooth = (_fitMode != FitMode::NoFit)
                     || kZoomLevels[_zoomIndex] <= 1.0;
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p.drawImage(dst, img);

    if (_cropMode) paintCropOverlay(p, dst);
}

void ViewerWidget::paintCropOverlay(QPainter& p, const QRect& imageRect) {
    // The selection is only meaningful where it overlaps the image.
    const QRect sel = _cropRect.normalized().intersected(imageRect);

    // Dim everything outside the selection (but only over the image — the
    // dark canvas beyond it needs no scrim). Four rects around `sel`.
    p.setRenderHint(QPainter::Antialiasing, false);
    const QColor scrim(0, 0, 0, 130);
    if (sel.isValid() && sel.width() > 0 && sel.height() > 0) {
        p.fillRect(QRect(imageRect.left(), imageRect.top(),
                         imageRect.width(), sel.top() - imageRect.top()), scrim);
        p.fillRect(QRect(imageRect.left(), sel.bottom() + 1,
                         imageRect.width(), imageRect.bottom() - sel.bottom()), scrim);
        p.fillRect(QRect(imageRect.left(), sel.top(),
                         sel.left() - imageRect.left(), sel.height()), scrim);
        p.fillRect(QRect(sel.right() + 1, sel.top(),
                         imageRect.right() - sel.right(), sel.height()), scrim);

        // Selection border + rule-of-thirds guides.
        p.setPen(QPen(QColor(255, 255, 255, 230), 1));
        p.drawRect(sel.adjusted(0, 0, -1, -1));
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i < 3; ++i) {
            const int x = sel.left() + sel.width()  * i / 3;
            const int y = sel.top()  + sel.height() * i / 3;
            p.drawLine(x, sel.top(), x, sel.bottom());
            p.drawLine(sel.left(), y, sel.right(), y);
        }
    } else {
        // Nothing selected yet — scrim the whole image to signal crop mode.
        p.fillRect(imageRect, scrim);
    }
}

void ViewerWidget::zoomIn() {
    if (_fitMode != FitMode::NoFit) {
        // First zoomIn from any fit mode lands at NoFit @ 100%, then
        // subsequent clicks step through kZoomLevels.
        _fitMode = FitMode::NoFit;
        _zoomIndex = kZoomIndex100;
    } else if (_zoomIndex + 1 < kZoomCount) {
        ++_zoomIndex;
    }
    updateCursor();
    update();
}

void ViewerWidget::zoomOut() {
    if (_fitMode != FitMode::NoFit) {
        _fitMode = FitMode::NoFit;
        _zoomIndex = kZoomIndex100;
    } else if (_zoomIndex > 0) {
        --_zoomIndex;
    }
    updateCursor();
    update();
}

int ViewerWidget::currentZoomPercent() const {
    // Fit modes don't map to a discrete percent — the menu uses 0 to
    // mean "no percentage row should be checked".
    if (_fitMode != FitMode::NoFit) return 0;
    return percentForIndex(_zoomIndex);
}

void ViewerWidget::setFitMode(FitMode mode) {
    if (_fitMode == mode) return;
    _fitMode = mode;
    if (mode != FitMode::NoFit) {
        // No pan in fit modes — the image either fills the widget or
        // sits at native size centered, neither of which has anywhere
        // to pan to.
        _translate = QPoint();
    }
    // Mode change can flip wantPan(), so re-evaluate before updating
    // the cursor (otherwise we'd show OpenHand under Fit briefly).
    endPanIfDone();
    updateCursor();
    update();
}

void ViewerWidget::setZoomPercent(int pct) {
    for (int i = 0; i < kZoomCount; ++i) {
        if (percentForIndex(i) == pct) {
            _fitMode = FitMode::NoFit;
            _zoomIndex = i;
            updateCursor();
            update();
            return;
        }
    }
}

void ViewerWidget::keyPressEvent(QKeyEvent* event) {
    // Crop mode owns the keyboard: Enter applies, Esc cancels, and everything
    // else is swallowed so navigation / zoom keys don't fire mid-crop.
    if (_cropMode) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            applyCrop();
            break;
        case Qt::Key_Escape:
            cancelCrop();
            break;
        default:
            break;
        }
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit dismissed();
        event->accept();
        return;
    case Qt::Key_R:
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) break;
        if (event->modifiers() & Qt::ShiftModifier) rotateLeft();
        else                                        rotateRight();
        event->accept();
        return;
    case Qt::Key_H:
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) break;
        flipHorizontal();
        event->accept();
        return;
    case Qt::Key_V:
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) break;
        flipVertical();
        event->accept();
        return;
    case Qt::Key_C:
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) break;
        beginCrop();
        event->accept();
        return;
    case Qt::Key_Left:
        emit prevRequested();
        event->accept();
        return;
    case Qt::Key_Right:
        emit nextRequested();
        event->accept();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomOut();
        event->accept();
        return;
    case Qt::Key_Asterisk:
        setZoomPercent(100);
        event->accept();
        return;
    case Qt::Key_Slash:
        setFitMode(FitMode::FitLargeOnly);
        event->accept();
        return;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            _spaceDown = true;
            // Anchor at the cursor's current position over the widget.
            // mapFromGlobal gives the right result even if the mouse
            // is outside the widget (negative or out-of-bounds — the
            // delta math in mouseMoveEvent still works once it enters).
            beginPanIfNeeded(mapFromGlobal(QCursor::pos()));
            updateCursor();
        }
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ViewerWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        _spaceDown = false;
        endPanIfDone();
        updateCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void ViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (_cropMode && event->button() == Qt::LeftButton) {
        _cropDragging = true;
        _cropStart = event->pos();
        _cropRect = QRect(_cropStart, _cropStart);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        _midDown = true;
        beginPanIfNeeded(event->pos());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (_cropMode) {
        if (_cropDragging) {
            _cropRect = QRect(_cropStart, event->pos());
            update();
            event->accept();
        }
        return;
    }
    if (_panning) {
        _translate = event->pos() - _panStart;
        clampTranslate();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (_cropMode && event->button() == Qt::LeftButton) {
        _cropDragging = false;
        // A drag that produced a usable rectangle commits the crop on release;
        // an accidental click (near-zero rect) just leaves the marquee empty so
        // the user can drag again. Enter also applies, Esc cancels.
        _cropRect = QRect(_cropStart, event->pos()).normalized();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        _midDown = false;
        endPanIfDone();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewerWidget::focusOutEvent(QFocusEvent* event) {
    // Losing focus (Alt+Tab, click into another widget) means we won't
    // see the matching key/button release. Reset the pan triggers so we
    // don't come back panning unexpectedly when the user returns.
    if (_spaceDown || _midDown || _panning) {
        _spaceDown = false;
        _midDown = false;
        _panning = false;
        updateCursor();
    }
    QWidget::focusOutEvent(event);
}

bool ViewerWidget::wantPan() const {
    if (_cropMode) return false;   // the mouse is defining the crop marquee
    return (_spaceDown && _fitMode == FitMode::NoFit) || _midDown;
}

void ViewerWidget::beginPanIfNeeded(const QPoint& mousePos) {
    if (_panning || !wantPan()) return;
    _panning = true;
    _panStart = mousePos - _translate;
}

void ViewerWidget::endPanIfDone() {
    if (!_panning || wantPan()) return;
    _panning = false;
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (_cropMode) {   // don't dismiss while cropping — a double-click is part of a drag
        event->accept();
        return;
    }
    emit dismissed();
    event->accept();
}

void ViewerWidget::wheelEvent(QWheelEvent* event) {
    const int dy = event->angleDelta().y();
    if (dy == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        if (dy > 0) zoomIn();
        else        zoomOut();
        event->accept();
        return;
    }
    if (dy > 0) emit prevRequested();
    else        emit nextRequested();
    event->accept();
}

void ViewerWidget::updateCursor() {
    if (_cropMode) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (currentImage().isNull() || _fitMode != FitMode::NoFit) {
        unsetCursor();
        return;
    }
    if (_panning) {
        setCursor(Qt::ClosedHandCursor);
    } else if (_spaceDown) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
}
