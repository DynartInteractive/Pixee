#include "ViewerWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QWheelEvent>

namespace {
constexpr double kZoomLevels[] = {
    0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0
};
constexpr int kZoomCount = static_cast<int>(sizeof(kZoomLevels) / sizeof(double));
constexpr int kZoomIndex100 = 4;  // index of 1.0 above
}

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(1, 1);
    setAutoFillBackground(false);
    setMouseTracking(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    _zoomIndex = kZoomIndex100;
}

void ViewerWidget::setImage(const QImage& image) {
    _image = image;
    // Reset to fit + centered each time we switch to a new image — matches
    // Pixie's behaviour and is the expected default for "I just opened
    // the next photo". Rotation also resets per-image.
    _fit = true;
    _zoomIndex = kZoomIndex100;
    _translate = QPoint();
    _rotation = 0;
    _rotatedImage = QImage();
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
    if (_fit) {
        return img.size().scaled(size(), Qt::KeepAspectRatio);
    }
    const double z = kZoomLevels[_zoomIndex];
    return QSize(int(img.width() * z), int(img.height() * z));
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

    // Smooth for downscale (fit / zoom < 1.0); nearest for upscale (>1.0)
    // so pixel art stays crisp when zooming above 1:1 — same convention
    // as the thumbnail upscale path.
    const bool smooth = _fit || kZoomLevels[_zoomIndex] <= 1.0;
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p.drawImage(dst, img);
}

void ViewerWidget::zoomIn() {
    if (_fit) {
        _fit = false;
        _zoomIndex = kZoomIndex100;
    } else if (_zoomIndex + 1 < kZoomCount) {
        ++_zoomIndex;
    }
    update();
}

void ViewerWidget::zoomOut() {
    if (_fit) {
        _fit = false;
        _zoomIndex = kZoomIndex100;
    } else if (_zoomIndex > 0) {
        --_zoomIndex;
    }
    update();
}

void ViewerWidget::toggleFit() {
    _fit = !_fit;
    if (_fit) {
        _translate = QPoint();
    } else {
        _zoomIndex = kZoomIndex100;
    }
    updateCursor();
    update();
}

void ViewerWidget::actualSize() {
    _fit = false;
    _zoomIndex = kZoomIndex100;
    _translate = QPoint();
    updateCursor();
    update();
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
    case Qt::Key_0:
    case Qt::Key_Asterisk:
        toggleFit();
        event->accept();
        return;
    case Qt::Key_1:
        actualSize();
        event->accept();
        return;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            _spaceDown = true;
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
        updateCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void ViewerWidget::mousePressEvent(QMouseEvent* event) {
    const bool spacePan  = event->button() == Qt::LeftButton  && _spaceDown;
    const bool middlePan = event->button() == Qt::MiddleButton;
    if (!_fit && (spacePan || middlePan)) {
        _panning = true;
        _panStart = event->pos() - _translate;
        updateCursor();
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
    if (_panning && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        _panning = false;
        updateCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
    if (currentImage().isNull() || _fit) {
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
