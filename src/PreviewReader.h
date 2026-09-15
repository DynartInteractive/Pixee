#ifndef PREVIEWREADER_H
#define PREVIEWREADER_H

#include <QAtomicInt>
#include <QImage>
#include <QObject>
#include <QString>

// Loads a downscaled copy of an image off the GUI thread. Used by the
// histogram dock while *browsing* — in the viewer the panel is fed from
// ViewerWidget::previewUpdated instead, because the viewer already holds a
// proxy of exactly the pixels it is painting.
//
// Same supersede-on-navigate pattern as MetadataReader / ImageLoader: a read
// carries a taskVersion snapshot and bails with `aborted` once the live
// counter has moved on, so arrowing down a folder on an SMB share never builds
// a backlog of decodes nobody is waiting for any more.
//
// The decode is deliberately *not* scaled, which was not the first design.
// QImageReader::setScaledSize looked like free speed, but measuring it killed
// the idea twice over: it only has a fast path on JPEG (PNG decodes in full and
// then downsamples, so a 4000x3000 PNG was *slower* scaled — 275 ms vs 239 ms),
// and downscaling averages neighbouring pixels, which drags clipped values back
// off the rails. On a test image it understated blown highlights as 0.08% when
// the true figure was 0.57%. That readout is the main reason to watch a
// histogram, so it has to be right.
//
// Histogram::compute subsamples on a grid instead, which keeps real pixel
// values and so estimates clipping without bias. The cost is holding one
// full-size QImage briefly; the read is off-thread, debounced and superseded,
// so that never blocks the UI.
class PreviewReader : public QObject
{
    Q_OBJECT
public:
    explicit PreviewReader(QAtomicInt* abortVersion, QObject* parent = nullptr);

public slots:
    void read(QString path, int taskVersion);

signals:
    void ready(QString path, QImage preview);
    void failed(QString path);
    void aborted(QString path);

private:
    bool isAborted(int taskVersion) const;
    QAtomicInt* _abortVersion;
};

#endif // PREVIEWREADER_H
