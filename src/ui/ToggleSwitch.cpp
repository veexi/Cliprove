#include "ToggleSwitch.h"

#include <QPainter>
#include <QPaintEvent>

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(42, 24);
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF track = QRectF(1, 2, width() - 2, height() - 4);
    const QColor off = palette().midlight().color();
    const QColor on(QStringLiteral("#10b981"));
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? on : off);
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);

    const qreal d = track.height() - 4;
    const qreal x = isChecked()
        ? track.right() - d - 2
        : track.left() + 2;
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(x, track.top() + 2, d, d));
}
