#include "Toast.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>

namespace {
// Only one toast at a time per process — keeps stacking-logic complexity
// out of v1. A new show() replaces whatever's already up.
QPointer<Toast> g_current;

constexpr int kFadeInMs = 150;
constexpr int kFadeOutMs = 250;
constexpr int kMargin = 16;
}

void Toast::show(QWidget* parent, const QString& message, Level level, int durationMs) {
    if (!parent || message.isEmpty()) return;

    if (g_current) {
        g_current->deleteLater();
        g_current.clear();
    }

    auto* t = new Toast(message, level, parent);
    g_current = t;

    t->reposition();
    t->QFrame::show();
    t->raise();

    auto* fadeIn = new QPropertyAnimation(t->_opacity, "opacity", t);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setDuration(kFadeInMs);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    QPointer<Toast> guard(t);
    QTimer::singleShot(durationMs, t, [guard]() {
        if (!guard) return;
        auto* fadeOut = new QPropertyAnimation(guard->_opacity, "opacity", guard);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        fadeOut->setDuration(kFadeOutMs);
        QObject::connect(fadeOut, &QPropertyAnimation::finished,
                         guard.data(), &QObject::deleteLater);
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

Toast::Toast(const QString& message, Level level, QWidget* parent)
    : QFrame(parent) {
    setObjectName("toast");
    setFrameShape(QFrame::StyledPanel);
    // The toast just shows information — don't steal hit testing from the
    // widgets underneath. A user clicking through it goes to whatever's
    // there, exactly as if the toast wasn't on screen.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Drives the per-level QSS rule below: #toast[level="error"] { ... }
    switch (level) {
    case Info:    setProperty("level", "info"); break;
    case Warning: setProperty("level", "warning"); break;
    case Error:   setProperty("level", "error"); break;
    }

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(0);

    _label = new QLabel(message, this);
    _label->setObjectName("toastLabel");
    _label->setWordWrap(true);
    _label->setTextFormat(Qt::PlainText);
    lay->addWidget(_label);

    setMaximumWidth(qMin(420, parent->width() - 2 * kMargin));
    adjustSize();

    _opacity = new QGraphicsOpacityEffect(this);
    _opacity->setOpacity(0.0);
    setGraphicsEffect(_opacity);

    // Reposition when the parent resizes so the toast hugs the corner.
    parent->installEventFilter(this);
}

void Toast::reposition() {
    if (!parentWidget()) return;
    move(parentWidget()->width() - width() - kMargin,
         parentWidget()->height() - height() - kMargin);
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QFrame::eventFilter(watched, event);
}
