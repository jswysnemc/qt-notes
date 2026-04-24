#include "ui/iconfactory.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

QIcon iconFromPainter(const std::function<void(QPainter &)> &paintCallback)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintCallback(painter);

    return QIcon(pixmap);
}

QPen themedPen(const QColor &color)
{
    QPen pen(color);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

} // namespace

QIcon IconFactory::listIcon(const QColor &color)
{
    return iconFromPainter([&color](QPainter &painter) {
        painter.setPen(themedPen(color));
        painter.drawLine(QPointF(4.0, 5.0), QPointF(14.0, 5.0));
        painter.drawLine(QPointF(4.0, 9.0), QPointF(14.0, 9.0));
        painter.drawLine(QPointF(4.0, 13.0), QPointF(14.0, 13.0));
    });
}

QIcon IconFactory::paletteIcon(const QColor &color)
{
    return iconFromPainter([&color](QPainter &painter) {
        painter.setPen(themedPen(color));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(3.5, 3.0, 11.0, 11.0));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(6.0, 5.0, 2.5, 2.5));
        painter.drawEllipse(QRectF(10.0, 6.0, 2.5, 2.5));
        painter.drawEllipse(QRectF(8.0, 10.0, 2.5, 2.5));
    });
}

QIcon IconFactory::settingsIcon(const QColor &color)
{
    return iconFromPainter([&color](QPainter &painter) {
        painter.setPen(themedPen(color));
        painter.drawLine(QPointF(4.0, 5.0), QPointF(14.0, 5.0));
        painter.drawLine(QPointF(4.0, 9.0), QPointF(14.0, 9.0));
        painter.drawLine(QPointF(4.0, 13.0), QPointF(14.0, 13.0));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(7.0, 3.2, 3.5, 3.5));
        painter.drawEllipse(QRectF(10.0, 7.2, 3.5, 3.5));
        painter.drawEllipse(QRectF(5.0, 11.2, 3.5, 3.5));
    });
}

QIcon IconFactory::plusIcon(const QColor &color)
{
    return iconFromPainter([&color](QPainter &painter) {
        painter.setPen(themedPen(color));
        painter.drawLine(QPointF(9.0, 4.0), QPointF(9.0, 14.0));
        painter.drawLine(QPointF(4.0, 9.0), QPointF(14.0, 9.0));
    });
}

QIcon IconFactory::closeIcon(const QColor &color)
{
    return iconFromPainter([&color](QPainter &painter) {
        painter.setPen(themedPen(color));
        painter.drawLine(QPointF(5.0, 5.0), QPointF(13.0, 13.0));
        painter.drawLine(QPointF(13.0, 5.0), QPointF(5.0, 13.0));
    });
}

QIcon IconFactory::swatchIcon(const QColor &fillColor, const QColor &borderColor)
{
    return iconFromPainter([&fillColor, &borderColor](QPainter &painter) {
        painter.setPen(QPen(borderColor, 1.0));
        painter.setBrush(fillColor);
        painter.drawRoundedRect(QRectF(2.5, 2.5, 13.0, 13.0), 4.0, 4.0);
    });
}
