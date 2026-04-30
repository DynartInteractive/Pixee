#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QImage>
#include <QWidget>

// Phase 1 image viewer: shows a single QImage scaled to fit the widget,
// keeping aspect ratio. No pan / zoom yet — Phase 3 adds those. Emits
// `dismissed()` when the user wants to leave the viewer (Esc, Enter, or
// double-click). MainWindow listens and swaps the central stack back.
class ViewerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ViewerWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clear();
    bool hasImage() const { return !_image.isNull(); }

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QImage _image;
};

#endif // VIEWERWIDGET_H
