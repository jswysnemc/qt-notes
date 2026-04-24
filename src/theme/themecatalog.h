#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct ThemeSpec {
    QString id;
    QString name;
    QColor surfaceColor;
    QColor titleBarColor;
    QColor editorColor;
    QColor borderColor;
    QColor textColor;
    QColor mutedTextColor;
    QColor accentColor;
    QColor selectionColor;
    QColor selectionTextColor;
    QColor hoverColor;
};

class ThemeCatalog
{
public:
    static const QVector<ThemeSpec> &themes();
    static ThemeSpec themeById(const QString &themeId);
};
