#include "ViewerWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QWheelEvent>

namespace {
constexpr double kZoomLevels[] = {
    0.10, 0.25, 0.50, 0.75, 1.0, 2.0, 4.0, 6.0, 8.0, 12.0, 16.0
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

void ViewerWidget::setImage(const QImage& image) {
    _image = image;
    // Rotation is per-image regardless of lockZoom — it's a transform on
    // the image data, not a property of the view.
    _rotation = 0;
    _rotatedImage = QImage();
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
    _rotation = 0;
    _rotatedImage = QImage();
    if (!_lockZoom) {
        _fitMode = FitMode::Fit;
        _zoomIndex = kZoomIndex100;
        _translate = QPoint();
    }
    update();
}

void ViewerWidget::updateImage(const QImage& image) {
    _image = image;
    // Refresh the rotated cache for the new pixels (no-op when rotation == 0).
    invalidateRotation();
    update();
}

void ViewerWidget::clear() {
    _image = QImage();
    _rotatedImage = QImage();
    _rotation = 0;
    _translate = QPoint();
    update();
}

const QImage& ViewerWidget::currentImage() const {
    return (_rotation != 0 && !_rotatedImage.isNull()) ? _rotatedImage : _image;
}

void ViewerWidget::invalidateRotation() {
    if (_rotation == 0 || _image.isNull()) {
        _rotatedImage = QImage();
        return;
    }
    QTransform xform;
    xform.rotate(_rotation);
    _rotatedImage = _image.transformed(xform, Qt::SmoothTransformation);
}

void ViewerWidget::rotateLeft() {
    _rotation = (_rotation + 270) % 360;  // -90, normalised
    invalidateRotation();
    _translate = QPoint();                // reset pan; aspect changed
    update();
}

void ViewerWidget::rotateRight() {
    _rotation = (_rotation + 90) % 360;
    invalidateRotation();
    _translate = QPoint();
    update();
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
    const QSize ds = currentDrawSize();
    if (ds.isEmpty()) return;

    const QPoint center(width() / 2, height() / 2);
    const QPoint topLeft(
        center.x() - ds.width()  / 2 + _translate.x(),
        center.y() - ds.height() / 2 + _translate.y());
    const QRect dst(topLeft, ds);

    // Smooth for downscale (any fit mode + NoFit < 1.0); nearest for
    // upscale in NoFit (>1.0) so pixel art stays crisp when zooming
    // above 1:1 — same convention as the thumbnail upscale path.
    const bool smooth = (_fitMode != FitMode::NoFit)
                     || kZoomLevels[_zoomIndex] <= 1.0;
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p.drawImage(dst, img);
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
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit dismissed();
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
    if (event->button() == Qt::MiddleButton) {
        _midDown = true;
        beginPanIfNeeded(event->pos());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
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
