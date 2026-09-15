#include "HistogramPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <cmath>

namespace {

// Histograms are read on a dark ground by convention, and the additive channel
// blend below only works over black - so the plot paints its own background
// rather than following the palette.
const QColor kPlotBackground(24, 24, 24);
const QColor kPlotGrid(255, 255, 255, 28);
const QColor kRed(255, 64, 64);
const QColor kGreen(64, 255, 64);
const QColor kBlue(80, 110, 255);
const QColor kLuma(225, 225, 225);

// The plot is banded rather than free to expand. Left to fill a full-height
// dock it becomes ~1700px tall, which wastes the column, pushes the clipping
// readout far away from the graph it describes, and buys no extra information
// - the useful detail in a histogram is horizontal, across the 256 bins.
// The minimum is deliberately low. Stacked under Metadata and Adjust, this
// dock has to be able to give height back, or three open docks would force the
// window taller than the 1280x720 floor; a histogram still reads fine at 80px.
constexpr int kPlotMinHeight = 80;
constexpr int kPlotMaxHeight = 220;

}  // namespace

// ---------------------------------------------------------------- HistogramView

HistogramView::HistogramView(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(kPlotMinHeight);
    setMaximumHeight(kPlotMaxHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void HistogramView::setData(const Histogram::Data& data) {
    _data = data;
    update();
}

void HistogramView::setMode(Mode mode) {
    if (_mode == mode) return;
    _mode = mode;
    update();
}

void HistogramView::setLogScale(bool on) {
    if (_log == on) return;
    _log = on;
    update();
}

double HistogramView::normalised(quint32 count) const {
    if (_data.peak == 0) return 0.0;
    if (!_log) return double(count) / double(_data.peak);
    // One tall spike - a flat sky, a letterboxed border - otherwise flattens
    // everything else onto the axis.
    return std::log1p(double(count)) / std::log1p(double(_data.peak));
}

void HistogramView::paintChannel(QPainter& p, const Histogram::Bins& bins,
                                 const QColor& colour, const QRect& plot) const {
    QPainterPath path;
    path.moveTo(plot.left(), plot.bottom());
    for (int i = 0; i < 256; ++i) {
        const double x = plot.left() + (plot.width() - 1) * (i / 255.0);
        const double y = plot.bottom() - normalised(bins[static_cast<size_t>(i)])
                                             * (plot.height() - 1);
        path.lineTo(x, y);
    }
    path.lineTo(plot.right(), plot.bottom());
    path.closeSubpath();

    QColor fill = colour;
    fill.setAlpha(150);
    p.setPen(QPen(colour, 1));
    p.setBrush(fill);
    p.drawPath(path);
}

void HistogramView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect plot = rect().adjusted(0, 0, -1, -1);
    p.fillRect(rect(), kPlotBackground);

    // Quarter guides - the eye needs a reference to judge where the mass sits.
    p.setPen(QPen(kPlotGrid, 1));
    for (int i = 1; i < 4; ++i) {
        const int x = plot.left() + plot.width() * i / 4;
        p.drawLine(x, plot.top(), x, plot.bottom());
    }

    if (_data.isEmpty()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No image"));
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    if (_mode == Mode::Luminance) {
        paintChannel(p, _data.luma, kLuma, plot);
    } else {
        // Additive so overlaps read as the colour they actually make: red over
        // green shows yellow, all three show white where the channels agree.
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        paintChannel(p, _data.red, kRed, plot);
        paintChannel(p, _data.green, kGreen, plot);
        paintChannel(p, _data.blue, kBlue, plot);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}

// --------------------------------------------------------------- HistogramPanel

HistogramPanel::HistogramPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName("histogramPanel");   // see the note in AdjustPanel's ctor

    // Matches the Adjust panel so the shared column does not jump width when
    // one of them is closed.
    setMinimumWidth(250);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* controls = new QHBoxLayout();
    _mode = new QComboBox(this);
    _mode->addItem(tr("RGB"));
    _mode->addItem(tr("Luminance"));
    connect(_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _view->setMode(index == 1 ? HistogramView::Mode::Luminance
                                  : HistogramView::Mode::Rgb);
    });
    controls->addWidget(_mode);
    controls->addStretch(1);

    _log = new QCheckBox(tr("Log"), this);
    _log->setToolTip(tr("Logarithmic scale - keeps one tall spike from "
                        "flattening the rest"));
    connect(_log, &QCheckBox::toggled, this,
            [this](bool on) { _view->setLogScale(on); });
    controls->addWidget(_log);
    outer->addLayout(controls);

    _view = new HistogramView(this);
    outer->addWidget(_view);

    _readout = new QLabel(this);
    _readout->setWordWrap(true);
    outer->addWidget(_readout);

    // Everything sits at the top of the dock; the slack goes below.
    outer->addStretch(1);

    updateReadout();
}

void HistogramPanel::setPreviewImage(const QImage& preview) {
    _data = Histogram::compute(preview);
    _view->setData(_data);
    updateReadout();
}

void HistogramPanel::updateReadout() {
    if (_data.isEmpty()) {
        _readout->clear();
        return;
    }
    const double total = double(_data.sampled);
    // The number worth watching while dragging contrast: how much of the image
    // has already been pushed off either end and cannot be brought back.
    _readout->setText(tr("Clipped - shadows %1%, highlights %2%")
                          .arg(100.0 * _data.clippedShadow / total, 0, 'f', 1)
                          .arg(100.0 * _data.clippedHighlight / total, 0, 'f', 1));
}
