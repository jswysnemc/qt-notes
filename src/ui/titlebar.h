#pragma once

#include <QWidget>

#include "theme/themecatalog.h"

class QLineEdit;
class QLabel;
class QStackedLayout;
class QToolButton;

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const;
    void setTheme(const ThemeSpec &theme);

    QWidget *themeButtonWidget() const;
    QWidget *listButtonWidget() const;

signals:
    void listRequested();
    void themeRequested();
    void settingsRequested();
    void newNoteRequested();
    void closeRequested();
    void titleEdited(const QString &title);
    void dragRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void beginEdit();
    void finishEdit(bool accept);
    void updateIcons();

    QWidget *titleSurface_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLineEdit *titleEdit_ = nullptr;
    QStackedLayout *titleStack_ = nullptr;
    QToolButton *listButton_ = nullptr;
    QToolButton *themeButton_ = nullptr;
    QToolButton *settingsButton_ = nullptr;
    QToolButton *newButton_ = nullptr;
    QToolButton *closeButton_ = nullptr;
    ThemeSpec theme_;
    bool titlePressed_ = false;
    QPoint pressPosition_;
    QString stableTitle_;
};
