#pragma once

#include <QDialog>
#include <QStringList>

#include "data/notedata.h"
#include "theme/themecatalog.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(const QString &noteTitle,
                   bool wrapMode,
                   const QString &fontFamily,
                   int fontPointSize,
                   const QStringList &recentFonts,
                   SortMode sortMode,
                   StartupNoteMode startupNoteMode,
                   const ThemeSpec &theme,
                   QWidget *parent = nullptr);

    bool wrapMode() const;
    QString fontFamily() const;
    int fontPointSize() const;
    SortMode sortMode() const;
    StartupNoteMode startupNoteMode() const;
    bool deleteRequested() const;

private:
    void applyTheme(const ThemeSpec &theme);
    void syncSelectedFont(const QString &fontFamily);

    QString noteTitle_;
    QCheckBox *wrapCheckBox_ = nullptr;
    QComboBox *recentFontsCombo_ = nullptr;
    QFontComboBox *fontFamilyCombo_ = nullptr;
    QSpinBox *fontSizeSpinBox_ = nullptr;
    QComboBox *sortModeCombo_ = nullptr;
    QComboBox *startupModeCombo_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    bool deleteRequested_ = false;
};
