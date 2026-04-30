#include "ViewerWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(1, 1);
    setAutoFillBackground(false);
}

void ViewerWidget::setImage(const QImage& image) {
    _image = image;
    update();
}

void ViewerWidget::clear() {
    _image = QImage();
    update();
}

void ViewerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 24));  // dark canvas; matches Pixie's view background

    if (_image.isNull()) return;

    // Fit-to-window: scale the image's bounding box into the widget while
    // preserving aspect ratio, then center it.
    const QSize fitted = _image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect dst = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, fitted, rect());
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(dst, _image);
}

void ViewerWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit dismissed();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    emit dismissed();
    event->accept();
}
