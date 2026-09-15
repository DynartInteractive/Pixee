#ifndef ADJUSTPANEL_H
#define ADJUSTPANEL_H

#include <QWidget>

#include "ImageAdjust.h"

class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSlider;
class QSpinBox;

// Live colour-adjustment controls for the viewer's current image. Lives in the
// right-side "Adjust" dock, tabbed with Metadata.
//
// The panel owns no pixels and does no image work: it is a view over an
// ImageAdjust::Adjustments value. Moving a control emits the whole value and
// ViewerWidget re-applies it to the pristine source, which is what keeps a
// slider drag from compounding (see the note at the top of ImageAdjust.h).
//
// A dock rather than a dialog: the sliders, the image and (later) the
// histogram all need to be visible at once, which a modal fights.
class AdjustPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AdjustPanel(QWidget* parent = nullptr);

    ImageAdjust::Adjustments adjustments() const;

public slots:
    // Push values into the controls *without* emitting adjustmentsChanged, so
    // the panel can follow the viewer (which resets on every image change)
    // without the update echoing straight back.
    void setAdjustments(const ImageAdjust::Adjustments& adjustments);
    // Back to defaults, and announce it.
    void reset();

signals:
    void adjustmentsChanged(ImageAdjust::Adjustments adjustments);

protected:
    // Watches the row labels so double-clicking one resets just that row.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Field { Brightness, Contrast, Saturation, Hue, Gamma, FieldCount };

    // Every row is label + slider + spin box. The spin is an int one for the
    // four plain fields; gamma needs a double because its slider is not linear
    // in the value it produces (see gammaForSlider in the .cpp).
    struct Row {
        QLabel*         label = nullptr;
        QSlider*        slider = nullptr;
        QSpinBox*       spin = nullptr;
        QDoubleSpinBox* gammaSpin = nullptr;
    };

    void addIntRow(QGridLayout* grid, int gridRow, Field field,
                   const QString& text, int min, int max, const QString& suffix);
    void addGammaRow(QGridLayout* grid, int gridRow);
    void wireRow(Field field);
    void resetField(Field field);
    void emitChanged();

    Row _rows[FieldCount];
    // Set while the panel is writing its own controls, so the slider/spin
    // mirror updates don't each fire a change of their own.
    bool _syncing = false;
};

#endif // ADJUSTPANEL_H
