#ifndef TOAST_H
#define TOAST_H

#include <QFrame>
#include <QString>

class QGraphicsOpacityEffect;
class QLabel;

// Non-modal transient notification anchored to the bottom-right of a
// parent widget. Use Toast::show(...) — it manages its own lifetime
// (fade-in, hold for `durationMs`, fade-out, deleteLater). Only one
// toast is shown at a time per parent; a new show() replaces the
// previous immediately.
class Toast : public QFrame
{
    Q_OBJECT
public:
    enum Level {
        Info,
        Warning,
        Error,
    };
    Q_ENUM(Level)

    static void show(QWidget* parent,
                     const QString& message,
                     Level level = Info,
                     int durationMs = 3500);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Toast(const QString& message, Level level, QWidget* parent);
    void reposition();

    QLabel* _label;
    QGraphicsOpacityEffect* _opacity;
};

#endif // TOAST_H
