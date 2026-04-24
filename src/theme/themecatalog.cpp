#include "theme/themecatalog.h"

const QVector<ThemeSpec> &ThemeCatalog::themes()
{
    static const QVector<ThemeSpec> kThemes = {
        { QStringLiteral("paper"),
          QStringLiteral("Paper"),
          QColor(QStringLiteral("#FFF5E6")),
          QColor(QStringLiteral("#F7E8C8")),
          QColor(QStringLiteral("#FFF9F0")),
          QColor(QStringLiteral("#E0CC9B")),
          QColor(QStringLiteral("#3C3022")),
          QColor(QStringLiteral("#7C664B")),
          QColor(QStringLiteral("#D99A34")),
          QColor(QStringLiteral("#E9BE62")),
          QColor(QStringLiteral("#2F2618")),
          QColor(QStringLiteral("#F2DCA8")) },
        { QStringLiteral("mint"),
          QStringLiteral("Mint"),
          QColor(QStringLiteral("#EAF7F1")),
          QColor(QStringLiteral("#D5EFE3")),
          QColor(QStringLiteral("#F5FCF8")),
          QColor(QStringLiteral("#A9D2BF")),
          QColor(QStringLiteral("#223A31")),
          QColor(QStringLiteral("#5A7A6D")),
          QColor(QStringLiteral("#38A377")),
          QColor(QStringLiteral("#8FDFB7")),
          QColor(QStringLiteral("#112118")),
          QColor(QStringLiteral("#D6F0E3")) },
        { QStringLiteral("ocean"),
          QStringLiteral("Ocean"),
          QColor(QStringLiteral("#EAF2FF")),
          QColor(QStringLiteral("#D7E5FF")),
          QColor(QStringLiteral("#F6F9FF")),
          QColor(QStringLiteral("#9EB5E7")),
          QColor(QStringLiteral("#24324A")),
          QColor(QStringLiteral("#5A6B87")),
          QColor(QStringLiteral("#4B82E6")),
          QColor(QStringLiteral("#89ADF6")),
          QColor(QStringLiteral("#152033")),
          QColor(QStringLiteral("#DCE8FF")) },
        { QStringLiteral("rose"),
          QStringLiteral("Rose"),
          QColor(QStringLiteral("#FFF0F4")),
          QColor(QStringLiteral("#FCE0E8")),
          QColor(QStringLiteral("#FFF8FA")),
          QColor(QStringLiteral("#E8B5C4")),
          QColor(QStringLiteral("#4A2732")),
          QColor(QStringLiteral("#8A5C6C")),
          QColor(QStringLiteral("#D35E89")),
          QColor(QStringLiteral("#F3A5C0")),
          QColor(QStringLiteral("#31131E")),
          QColor(QStringLiteral("#FBE0E8")) },
        { QStringLiteral("graphite"),
          QStringLiteral("Graphite"),
          QColor(QStringLiteral("#232629")),
          QColor(QStringLiteral("#2D3136")),
          QColor(QStringLiteral("#1F2226")),
          QColor(QStringLiteral("#4B535B")),
          QColor(QStringLiteral("#E9EDF1")),
          QColor(QStringLiteral("#A9B4C0")),
          QColor(QStringLiteral("#89B4FF")),
          QColor(QStringLiteral("#44628C")),
          QColor(QStringLiteral("#F8FBFF")),
          QColor(QStringLiteral("#39414A")) }
    };

    return kThemes;
}

ThemeSpec ThemeCatalog::themeById(const QString &themeId)
{
    for (const ThemeSpec &theme : themes()) {
        if (theme.id == themeId) {
            return theme;
        }
    }

    return themes().front();
}
