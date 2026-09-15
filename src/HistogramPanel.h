#ifndef HISTOGRAMPANEL_H
#define HISTOGRAMPANEL_H

#include <QImage>
#include <QWidget>

#include "Histogram.h"

class QCheckBox;
class QComboBox;
class QLabel;

// The graph itself, split out so the surrounding controls can sit in a normal
// layout while this one widget owns its whole rectangle.
class HistogramView : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Rgb, Luminance };

    explicit HistogramView(QWidget* parent = nullptr);

    void setData(const Histogram::Data& data);
    void setMode(Mode mode);
    void setLogScale(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Bin height as a fraction of the plot, 0..1.
    double normalised(quint32 count) const;
    void paintChannel(QPainter& p, const Histogram::Bins& bins,
                      const QColor& colour, const QRect& plot) const;

    Histogram::Data _data;
    Mode _mode = Mode::Rgb;
    bool _log = false;
};

// Live tone distribution for the image in the viewer, including any pending
// colour adjustment. Fed from ViewerWidget::previewUpdated - one producer for
// both the picture and the graph, so the two can never disagree.
//
// Always covers the whole image, never the visible crop, so panning and
// zooming do not reshape it.
class HistogramPanel : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramPanel(QWidget* parent = nullptr);

public slots:
    // Recount from the preview pixels. A null image empties the panel.
    void setPreviewImage(const QImage& preview);

private:
    void updateReadout();

    HistogramView* _view = nullptr;
    QComboBox* _mode = nullptr;
    QCheckBox* _log = nullptr;
    QLabel* _readout = nullptr;
    Histogram::Data _data;
};

#endif // HISTOGRAMPANEL_H
