#include "ViewerWidget.h"

#include <QCheckBox>
#include <QCursor>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>

#include <numeric>

namespace {
constexpr double kZoomLevels[] = {
    0.10, 0.25, 0.50, 0.75, 1.0, 1.25, 1.50, 2.0, 4.0, 6.0, 8.0, 12.0, 16.0
};
constexpr int kZoomCount = static_cast<int>(sizeof(kZoomLevels) / sizeof(double));
constexpr int kZoomIndex100 = 4;  // index of 1.0 above

int percentForIndex(int i) {
    return int(kZoomLevels[i] * 100.0 + 0.5);
}

// Crop marquee metrics, in widget pixels.
constexpr int kHandleSize = 9;    // painted square; odd so it centres on a line
constexpr int kHandleGrab = 15;   // hit-test square, deliberately larger
constexpr int kAntsPeriod = 90;   // ms between marching-ants steps
constexpr int kAntsDash   = 4;    // dash / gap length; the phase wraps at 2x
constexpr int kMinCropPx  = 4;    // below this a marquee counts as "no crop"

QPoint clampToRect(const QPoint& p, const QRect& r) {
    if (r.isEmpty()) return p;
    return QPoint(qBound(r.left(), p.x(), r.right()),
                  qBound(r.top(),  p.y(), r.bottom()));
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
    _cropHandle = CropHandle::None;
    _cropRect = QRect();
    // Pan makes no sense mid-crop; drop any drag state and switch the cursor.
    _spaceDown = _midDown = _panning = false;

    buildCropBar();
    positionCropBar();
    _cropBar->show();
    _cropBar->raise();

    if (!_antsTimer) {
        _antsTimer = new QTimer(this);
        _antsTimer->setInterval(kAntsPeriod);
        connect(_antsTimer, &QTimer::timeout, this, [this] {
            _antsPhase = (_antsPhase + 1) % (kAntsDash * 2);
            // Only the marquee border animates, so skip the repaint until
            // there is one — entering crop mode shouldn't spin the CPU.
            if (!_cropRect.normalized().isEmpty()) update();
        });
    }
    _antsPhase = 0;
    _antsTimer->start();

    setCursor(Qt::CrossCursor);
    update();
    emit cropModeChanged(true);
}

void ViewerWidget::endCropMode() {
    _cropMode = false;
    _cropDragging = false;
    _cropHandle = CropHandle::None;
    _cropRect = QRect();
    if (_antsTimer) _antsTimer->stop();
    if (_cropBar) _cropBar->hide();
    updateCursor();
}

void ViewerWidget::cancelCrop() {
    if (!_cropMode) return;
    endCropMode();
    update();
    emit cropModeChanged(false);
}

void ViewerWidget::applyCrop() {
    if (!_cropMode) return;
    // Too small a selection is treated as "no crop" — just leave crop mode.
    const QRect imgRect = cropRectInImage();
    if (imgRect.width() < 1 || imgRect.height() < 1) {
        cancelCrop();
        return;
    }
    const QImage cropped = currentImage().copy(imgRect);
    // Leave crop mode first so commitEdit's repaint doesn't draw the overlay.
    endCropMode();
    commitEdit(cropped);          // bakes the crop, marks modified, repaints
    emit cropModeChanged(false);  // after commit so listeners see the new size
}

QRect ViewerWidget::cropRectInImage() const {
    const QRect sel = _cropRect.normalized().intersected(imageRectOnWidget());
    if (sel.width() < kMinCropPx || sel.height() < kMinCropPx) return QRect();
    QRect r = mapWidgetRectToImage(sel);
    if (r.width() < 1 || r.height() < 1) return QRect();
    if (!ratioLocked()) return r;
    // The widget-space marquee is only ratio-correct to within a pixel of
    // rounding, so snap here, in image pixels, where it actually matters.
    // Reducing to lowest terms first makes the result exact rather than
    // merely close: k*rw : k*rh is the ratio by construction.
    const int g = std::gcd(_ratioW->value(), _ratioH->value());
    const int rw = _ratioW->value() / g;
    const int rh = _ratioH->value() / g;
    const int k = qMin(r.width() / rw, r.height() / rh);
    if (k < 1) return r;
    r.setWidth(k * rw);
    r.setHeight(k * rh);
    return r;
}

bool ViewerWidget::ratioLocked() const {
    return _ratioCheck && _ratioCheck->isChecked()
        && _ratioW && _ratioH && _ratioW->value() > 0 && _ratioH->value() > 0;
}

double ViewerWidget::lockedRatio() const {
    if (!ratioLocked()) return 0.0;
    return double(_ratioW->value()) / double(_ratioH->value());
}

void ViewerWidget::reapplyRatio() {
    if (!_cropMode) return;
    const QRect sel = _cropRect.normalized();
    // Pin the top-left and re-derive the far corner under the new ratio.
    if (!sel.isEmpty()) _cropRect = rectFromCorner(sel.topLeft(), sel.bottomRight());
    update();
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

QRect ViewerWidget::handleRect(CropHandle h, const QRect& sel) const {
    QPoint c;
    switch (h) {
    case CropHandle::TopLeft:     c = sel.topLeft();                          break;
    case CropHandle::Top:         c = QPoint(sel.center().x(), sel.top());     break;
    case CropHandle::TopRight:    c = sel.topRight();                         break;
    case CropHandle::Right:       c = QPoint(sel.right(), sel.center().y());   break;
    case CropHandle::BottomRight: c = sel.bottomRight();                      break;
    case CropHandle::Bottom:      c = QPoint(sel.center().x(), sel.bottom());  break;
    case CropHandle::BottomLeft:  c = sel.bottomLeft();                       break;
    case CropHandle::Left:        c = QPoint(sel.left(), sel.center().y());    break;
    case CropHandle::Body:
    case CropHandle::None:        return QRect();
    }
    const int half = kHandleSize / 2;
    return QRect(c.x() - half, c.y() - half, kHandleSize, kHandleSize);
}

ViewerWidget::CropHandle ViewerWidget::handleAt(const QPoint& pos) const {
    const QRect sel = _cropRect.normalized();
    if (sel.isEmpty()) return CropHandle::None;
    // Corners are tested first: on a small marquee their grab boxes overlap
    // the edge handles, and resizing from a corner is the more useful default.
    static const CropHandle kOrder[] = {
        CropHandle::TopLeft, CropHandle::TopRight,
        CropHandle::BottomRight, CropHandle::BottomLeft,
        CropHandle::Top, CropHandle::Right,
        CropHandle::Bottom, CropHandle::Left
    };
    const int half = kHandleGrab / 2;
    for (CropHandle h : kOrder) {
        const QPoint c = handleRect(h, sel).center();
        if (QRect(c.x() - half, c.y() - half, kHandleGrab, kHandleGrab).contains(pos))
            return h;
    }
    // Not on a handle, but inside the marquee: grabbing here moves it whole.
    return sel.contains(pos) ? CropHandle::Body : CropHandle::None;
}

QPoint ViewerWidget::anchorFor(CropHandle h, const QRect& sel) const {
    switch (h) {
    case CropHandle::TopLeft:     return sel.bottomRight();
    case CropHandle::TopRight:    return sel.bottomLeft();
    case CropHandle::BottomRight: return sel.topLeft();
    case CropHandle::BottomLeft:  return sel.topRight();
    default:                      return sel.topLeft();
    }
}

QRect ViewerWidget::rectFromCorner(const QPoint& anchor, const QPoint& moving) const {
    const QRect bounds = imageRectOnWidget();
    if (bounds.isEmpty()) return QRect();
    const int sx = (moving.x() < anchor.x()) ? -1 : 1;
    const int sy = (moving.y() < anchor.y()) ? -1 : 1;
    int w = qAbs(moving.x() - anchor.x());
    int h = qAbs(moving.y() - anchor.y());
    // Room left to grow from the anchor in the direction being dragged.
    const int availW = qMax(0, (sx > 0) ? bounds.right()  - anchor.x()
                                        : anchor.x() - bounds.left());
    const int availH = qMax(0, (sy > 0) ? bounds.bottom() - anchor.y()
                                        : anchor.y() - bounds.top());
    w = qMin(w, availW);
    h = qMin(h, availH);
    const double r = lockedRatio();
    if (r > 0.0) {
        // Grow the short side so the marquee still covers the cursor, then
        // pull whichever side overflowed back inside the image.
        if (double(w) > double(h) * r) h = qRound(w / r);
        else                           w = qRound(h * r);
        if (w > availW) { w = availW; h = qRound(w / r); }
        if (h > availH) { h = availH; w = qRound(h * r); }
    }
    return QRect(anchor, QPoint(anchor.x() + sx * w, anchor.y() + sy * h)).normalized();
}

QRect ViewerWidget::rectFromEdge(CropHandle h, const QPoint& moving) const {
    const QRect bounds = imageRectOnWidget();
    QRect sel = _cropRect.normalized();
    if (bounds.isEmpty() || sel.isEmpty()) return sel;
    switch (h) {
    case CropHandle::Left:
        sel.setLeft(qBound(bounds.left(), moving.x(), sel.right() - 1));
        break;
    case CropHandle::Right:
        sel.setRight(qBound(sel.left() + 1, moving.x(), bounds.right()));
        break;
    case CropHandle::Top:
        sel.setTop(qBound(bounds.top(), moving.y(), sel.bottom() - 1));
        break;
    case CropHandle::Bottom:
        sel.setBottom(qBound(sel.top() + 1, moving.y(), bounds.bottom()));
        break;
    default:
        return sel;
    }
    const double r = lockedRatio();
    if (r <= 0.0) return sel;
    // The perpendicular dimension follows, centred so the marquee grows to
    // both sides of the edge being dragged. Clipping at the image border can
    // leave the live rect slightly off-ratio; cropRectInImage() snaps it back
    // exactly when the crop is actually taken.
    if (h == CropHandle::Left || h == CropHandle::Right) {
        const int newH = qMax(1, qRound(sel.width() / r));
        sel.setTop(sel.center().y() - newH / 2);
        sel.setHeight(newH);
    } else {
        const int newW = qMax(1, qRound(sel.height() * r));
        sel.setLeft(sel.center().x() - newW / 2);
        sel.setWidth(newW);
    }
    return sel.intersected(bounds);
}

QRect ViewerWidget::rectMoved(const QPoint& moving) const {
    const QRect bounds = imageRectOnWidget();
    QRect sel = _cropRect.normalized();
    if (bounds.isEmpty() || sel.isEmpty()) return sel;
    QPoint tl = moving - _cropMoveOffset;
    // Clamp so the marquee stays wholly on the image. qMax guards the
    // degenerate case of a marquee wider than the image, where the upper
    // bound would otherwise fall below the lower one and trip qBound.
    const int maxX = qMax(bounds.left(), bounds.right()  - sel.width()  + 1);
    const int maxY = qMax(bounds.top(),  bounds.bottom() - sel.height() + 1);
    tl.setX(qBound(bounds.left(), tl.x(), maxX));
    tl.setY(qBound(bounds.top(),  tl.y(), maxY));
    sel.moveTo(tl);
    return sel;
}

void ViewerWidget::setCropCursor(CropHandle h) {
    switch (h) {
    case CropHandle::TopLeft:
    case CropHandle::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case CropHandle::TopRight:
    case CropHandle::BottomLeft:  setCursor(Qt::SizeBDiagCursor); break;
    case CropHandle::Left:
    case CropHandle::Right:       setCursor(Qt::SizeHorCursor);   break;
    case CropHandle::Top:
    case CropHandle::Bottom:      setCursor(Qt::SizeVerCursor);   break;
    case CropHandle::Body:        setCursor(Qt::SizeAllCursor);   break;
    case CropHandle::None:        setCursor(Qt::CrossCursor);     break;
    }
}

void ViewerWidget::buildCropBar() {
    if (_cropBar) return;
    _cropBar = new QWidget(this);
    _cropBar->setObjectName("cropBar");
    _cropBar->setAutoFillBackground(true);
    // A palette rather than an inline stylesheet: a widget stylesheet would
    // outrank the app-level QSS, leaving themes unable to restyle #cropBar.
    QPalette pal = _cropBar->palette();
    pal.setColor(QPalette::Window, QColor(32, 32, 32));
    pal.setColor(QPalette::WindowText, Qt::white);
    _cropBar->setPalette(pal);

    _ratioCheck = new QCheckBox(tr("Fixed ratio"), _cropBar);
    _ratioW = new QSpinBox(_cropBar);
    _ratioH = new QSpinBox(_cropBar);
    // Set the palette on the label widgets too, not just the bar: once an
    // app-level stylesheet is installed, QStyleSheetStyle stops propagating a
    // parent's palette into child text, and the label would fall back to the
    // default near-black on this dark bar. Themes override via #cropBar QSS.
    _ratioCheck->setPalette(pal);
    for (QSpinBox* box : {_ratioW, _ratioH}) {
        box->setRange(1, 9999);
        box->setMaximumWidth(64);
        box->setEnabled(false);          // gated on the check box
    }
    _ratioW->setValue(2);
    _ratioH->setValue(3);

    auto* layout = new QHBoxLayout(_cropBar);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);
    layout->addWidget(_ratioCheck);
    layout->addWidget(_ratioW);
    auto* colon = new QLabel(QStringLiteral(":"), _cropBar);
    colon->setPalette(pal);
    layout->addWidget(colon);
    layout->addWidget(_ratioH);

    connect(_ratioCheck, &QCheckBox::toggled, this, [this](bool on) {
        _ratioW->setEnabled(on);
        _ratioH->setEnabled(on);
        reapplyRatio();
    });
    connect(_ratioW, &QSpinBox::valueChanged, this, [this](int) { reapplyRatio(); });
    connect(_ratioH, &QSpinBox::valueChanged, this, [this](int) { reapplyRatio(); });

    // Enter / Esc must keep working while the focus sits in the bar. The
    // filter goes on every descendant because a spin box delivers key events
    // to its internal QLineEdit, not to itself.
    _cropBar->installEventFilter(this);
    for (QObject* child : _cropBar->findChildren<QObject*>())
        child->installEventFilter(this);

    _cropBar->hide();
}

void ViewerWidget::positionCropBar() {
    if (!_cropBar) return;
    _cropBar->adjustSize();
    _cropBar->move(qMax(0, (width() - _cropBar->width()) / 2), 12);
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

        // Rule-of-thirds guides, drawn under the border.
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i < 3; ++i) {
            const int x = sel.left() + sel.width()  * i / 3;
            const int y = sel.top()  + sel.height() * i / 3;
            p.drawLine(x, sel.top(), x, sel.bottom());
            p.drawLine(sel.left(), y, sel.right(), y);
        }

        // Marching ants: a solid black rectangle with an animated white dashed
        // one on top. The two-tone border stays legible over both light and
        // dark pixels, and the moving dashes read as "this is a live
        // selection" rather than part of the picture.
        const QRect border = sel.adjusted(0, 0, -1, -1);
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(border);
        QPen ants(Qt::white, 1);
        ants.setDashPattern({qreal(kAntsDash), qreal(kAntsDash)});
        ants.setDashOffset(_antsPhase);
        p.setPen(ants);
        p.drawRect(border);

        paintCropHandles(p, sel);
        paintCropReadout(p);
    } else {
        // Nothing selected yet — scrim the whole image to signal crop mode.
        p.fillRect(imageRect, scrim);
    }
}

void ViewerWidget::paintCropHandles(QPainter& p, const QRect& sel) {
    // Opaque white square with a 1px black outline, so a handle is visible
    // whatever it happens to sit on top of.
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    static const CropHandle kAll[] = {
        CropHandle::TopLeft, CropHandle::Top, CropHandle::TopRight,
        CropHandle::Right, CropHandle::BottomRight, CropHandle::Bottom,
        CropHandle::BottomLeft, CropHandle::Left
    };
    for (CropHandle h : kAll) {
        const QRect r = handleRect(h, sel);
        if (!r.isNull()) p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    p.setBrush(Qt::NoBrush);
}

void ViewerWidget::paintCropReadout(QPainter& p) {
    // The size the crop will actually produce, in image pixels — not the
    // on-screen marquee, which is whatever the current zoom makes of it.
    const QRect img = cropRectInImage();
    if (img.width() < 1 || img.height() < 1) return;
    QString text = tr("%1 x %2 px").arg(img.width()).arg(img.height());
    if (ratioLocked()) {
        text += QStringLiteral("   %1:%2").arg(_ratioW->value()).arg(_ratioH->value());
    }
    const QFontMetrics fm(font());
    const int padX = 10;
    const int padY = 5;
    const int w = fm.horizontalAdvance(text) + padX * 2;
    const int h = fm.height() + padY * 2;
    const QRect box((width() - w) / 2, height() - h - 14, w, h);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(box, 4, 4);
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(240, 240, 240));
    p.drawText(box, Qt::AlignCenter, text);
    p.setRenderHint(QPainter::Antialiasing, false);
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
        _cropHandle = handleAt(event->pos());
        if (_cropHandle == CropHandle::None) {
            // Fresh marquee. The press is clamped onto the image so starting
            // the drag out on the surrounding canvas still works.
            _cropAnchor = clampToRect(event->pos(), imageRectOnWidget());
            _cropRect = QRect(_cropAnchor, _cropAnchor);
        } else if (_cropHandle == CropHandle::Body) {
            _cropMoveOffset = event->pos() - _cropRect.normalized().topLeft();
        } else {
            _cropAnchor = anchorFor(_cropHandle, _cropRect.normalized());
        }
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
            switch (_cropHandle) {
            case CropHandle::None:          // fresh marquee — free corner
            case CropHandle::TopLeft:
            case CropHandle::TopRight:
            case CropHandle::BottomRight:
            case CropHandle::BottomLeft:
                _cropRect = rectFromCorner(_cropAnchor, event->pos());
                break;
            case CropHandle::Body:
                _cropRect = rectMoved(event->pos());
                break;
            default:
                _cropRect = rectFromEdge(_cropHandle, event->pos());
                break;
            }
            update();
        } else {
            // Hover feedback: the cursor tells you which way a handle resizes.
            setCropCursor(handleAt(event->pos()));
        }
        event->accept();
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
        // Release only ends the drag — the marquee stays up so its handles can
        // be nudged. Enter applies, Esc cancels.
        _cropDragging = false;
        _cropHandle = CropHandle::None;
        setCropCursor(handleAt(event->pos()));
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

void ViewerWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (_cropMode) positionCropBar();
}

bool ViewerWidget::eventFilter(QObject* watched, QEvent* event) {
    // The crop bar's spin boxes swallow Enter and Esc, so intercept them here
    // and route them to the same handlers the viewer's keyPressEvent uses.
    if (_cropBar && event->type() == QEvent::KeyPress) {
        auto* w = qobject_cast<QWidget*>(watched);
        if (w && (w == _cropBar || _cropBar->isAncestorOf(w))) {
            switch (static_cast<QKeyEvent*>(event)->key()) {
            case Qt::Key_Escape:
                cancelCrop();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                applyCrop();
                return true;
            default:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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
        // Crop-mode cursor depends on what the pointer is over, not on pan
        // state, so re-run the same hover test the mouse handler uses.
        setCropCursor(handleAt(mapFromGlobal(QCursor::pos())));
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
