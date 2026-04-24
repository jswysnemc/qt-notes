#pragma once

#include <QColor>
#include <QIcon>

class IconFactory
{
public:
    static QIcon listIcon(const QColor &color);
    static QIcon paletteIcon(const QColor &color);
    static QIcon settingsIcon(const QColor &color);
    static QIcon plusIcon(const QColor &color);
    static QIcon closeIcon(const QColor &color);
    static QIcon swatchIcon(const QColor &fillColor, const QColor &borderColor);
};
