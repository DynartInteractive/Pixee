#include "AdjustPanel.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace {

// Gamma is the one field whose slider is not the value. A linear 0.1..10
// slider would park 1.0 (the identity) at a tenth of the travel and give the
// whole right-hand side to values nobody uses, so the slider is a symmetric
// -100..+100 like its neighbours and maps exponentially: 0 is 1.0, and each
// end is two stops away (0.25 and 4.0).
constexpr int kGammaSliderRange = 100;
constexpr double kGammaStopsPerEnd = 2.0;

double gammaForSlider(int value) {
    return std::pow(2.0, kGammaStopsPerEnd * value / double(kGammaSliderRange));
}

int sliderForGamma(double gamma) {
    if (gamma <= 0.0) return 0;
    return qRound(std::log2(gamma) * kGammaSliderRange / kGammaStopsPerEnd);
}

}  // namespace

AdjustPanel::AdjustPanel(QWidget* parent)
    : QWidget(parent) {
    // Named so the theme can reach the child labels. It has to: once an app
    // stylesheet is installed, QStyleSheetStyle stops handing a parent's
    // palette down to child text, so a QLabel with no rule of its own renders
    // in the default near-black and vanishes into the dark panel behind it.
    // Same lesson as #cropBar - see the Theme note in CLAUDE.md.
    setObjectName("adjustPanel");

    // Wide enough that label + slider + spin box all fit at the dock's default
    // width; without this the spin boxes are pushed out of view until the user
    // drags the splitter, which is a poor first impression of the panel.
    setMinimumWidth(250);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);   // the slider takes the slack

    addIntRow(grid, 0, Brightness, tr("Brightness"), -100, 100, QString());
    addIntRow(grid, 1, Contrast,   tr("Contrast"),   -100, 100, QString());
    addIntRow(grid, 2, Saturation, tr("Saturation"), -100, 100, QString());
    addIntRow(grid, 3, Hue,        tr("Hue"),        -180, 180,
              QStringLiteral("°"));
    addGammaRow(grid, 4);
    outer->addLayout(grid);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* resetAll = new QPushButton(tr("Reset all"), this);
    connect(resetAll, &QPushButton::clicked, this, &AdjustPanel::reset);
    buttons->addWidget(resetAll);
    outer->addLayout(buttons);

    // The before/after key is not discoverable anywhere else, and it is the
    // control people reach for most once they have moved a slider.
    auto* hint = new QLabel(tr("Hold B to compare with the original."), this);
    hint->setObjectName("adjustHint");   // themed dimmer than the row labels
    hint->setWordWrap(true);
    outer->addWidget(hint);

    outer->addStretch(1);

    for (int i = 0; i < FieldCount; ++i) wireRow(static_cast<Field>(i));
}

void AdjustPanel::addIntRow(QGridLayout* grid, int gridRow, Field field,
                            const QString& text, int min, int max,
                            const QString& suffix) {
    Row& row = _rows[field];

    row.label = new QLabel(text, this);
    row.label->setToolTip(tr("Double-click to reset"));
    row.label->installEventFilter(this);

    row.slider = new QSlider(Qt::Horizontal, this);
    row.slider->setRange(min, max);
    row.slider->setValue(0);

    row.spin = new QSpinBox(this);
    row.spin->setRange(min, max);
    row.spin->setValue(0);
    row.spin->setSuffix(suffix);
    row.spin->setKeyboardTracking(false);   // wait for Enter / focus-out
    row.spin->setFixedWidth(64);

    grid->addWidget(row.label,  gridRow, 0);
    grid->addWidget(row.slider, gridRow, 1);
    grid->addWidget(row.spin,   gridRow, 2);
}

void AdjustPanel::addGammaRow(QGridLayout* grid, int gridRow) {
    Row& row = _rows[Gamma];

    row.label = new QLabel(tr("Gamma"), this);
    row.label->setToolTip(tr("Double-click to reset"));
    row.label->installEventFilter(this);

    row.slider = new QSlider(Qt::Horizontal, this);
    row.slider->setRange(-kGammaSliderRange, kGammaSliderRange);
    row.slider->setValue(0);

    row.gammaSpin = new QDoubleSpinBox(this);
    row.gammaSpin->setDecimals(2);
    row.gammaSpin->setRange(gammaForSlider(-kGammaSliderRange),
                            gammaForSlider(kGammaSliderRange));
    row.gammaSpin->setSingleStep(0.05);
    row.gammaSpin->setValue(1.0);
    row.gammaSpin->setKeyboardTracking(false);
    row.gammaSpin->setFixedWidth(64);

    grid->addWidget(row.label,     gridRow, 0);
    grid->addWidget(row.slider,    gridRow, 1);
    grid->addWidget(row.gammaSpin, gridRow, 2);
}

void AdjustPanel::wireRow(Field field) {
    Row& row = _rows[field];

    connect(row.slider, &QSlider::valueChanged, this, [this, field](int value) {
        if (_syncing) return;
        Row& r = _rows[field];
        _syncing = true;
        if (r.spin)            r.spin->setValue(value);
        else if (r.gammaSpin)  r.gammaSpin->setValue(gammaForSlider(value));
        _syncing = false;
        emitChanged();
    });

    if (row.spin) {
        connect(row.spin, &QSpinBox::valueChanged, this, [this, field](int value) {
            if (_syncing) return;
            _syncing = true;
            _rows[field].slider->setValue(value);
            _syncing = false;
            emitChanged();
        });
    } else if (row.gammaSpin) {
        connect(row.gammaSpin, &QDoubleSpinBox::valueChanged,
                this, [this, field](double value) {
            if (_syncing) return;
            _syncing = true;
            _rows[field].slider->setValue(sliderForGamma(value));
            _syncing = false;
            emitChanged();
        });
    }
}

ImageAdjust::Adjustments AdjustPanel::adjustments() const {
    ImageAdjust::Adjustments a;
    a.brightness = _rows[Brightness].slider->value();
    a.contrast   = _rows[Contrast].slider->value();
    a.saturation = _rows[Saturation].slider->value();
    a.hue        = _rows[Hue].slider->value();
    // Read the spin, not the slider: it is the value the user sees, and it is
    // already rounded to the two decimals the panel displays.
    a.gamma      = _rows[Gamma].gammaSpin->value();
    return a;
}

void AdjustPanel::setAdjustments(const ImageAdjust::Adjustments& adjustments) {
    const ImageAdjust::Adjustments a = ImageAdjust::clamped(adjustments);
    _syncing = true;
    _rows[Brightness].slider->setValue(a.brightness);
    _rows[Brightness].spin->setValue(a.brightness);
    _rows[Contrast].slider->setValue(a.contrast);
    _rows[Contrast].spin->setValue(a.contrast);
    _rows[Saturation].slider->setValue(a.saturation);
    _rows[Saturation].spin->setValue(a.saturation);
    _rows[Hue].slider->setValue(a.hue);
    _rows[Hue].spin->setValue(a.hue);
    _rows[Gamma].slider->setValue(sliderForGamma(a.gamma));
    _rows[Gamma].gammaSpin->setValue(a.gamma);
    _syncing = false;
}

void AdjustPanel::reset() {
    setAdjustments(ImageAdjust::Adjustments());
    emitChanged();
}

void AdjustPanel::resetField(Field field) {
    // Zero is the identity for every slider, gamma's included.
    _rows[field].slider->setValue(0);
}

void AdjustPanel::emitChanged() {
    emit adjustmentsChanged(adjustments());
}

bool AdjustPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (int i = 0; i < FieldCount; ++i) {
            if (_rows[i].label == watched) {
                resetField(static_cast<Field>(i));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
