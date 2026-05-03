#include "ui/titlebar.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QToolButton>

#include "data/notedata.h"
#include "ui/iconfactory.h"

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(42);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 10, 8);
    layout->setSpacing(6);

    titleSurface_ = new QWidget(this);
    titleSurface_->setObjectName(QStringLiteral("titleSurface"));
    titleSurface_->installEventFilter(this);

    titleStack_ = new QStackedLayout(titleSurface_);
    titleStack_->setContentsMargins(0, 0, 0, 0);

    titleLabel_ = new QLabel(titleSurface_);
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    titleLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    titleLabel_->installEventFilter(this);
    titleStack_->addWidget(titleLabel_);

    titleEdit_ = new QLineEdit(titleSurface_);
    titleEdit_->setObjectName(QStringLiteral("titleEdit"));
    titleEdit_->installEventFilter(this);
    titleStack_->addWidget(titleEdit_);

    layout->addWidget(titleSurface_, 1);

    auto createButton = [this, layout](const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("titleButton"));
        button->setFixedSize(28, 28);
        button->setAutoRaise(true);
        button->setToolTip(toolTip);
        layout->addWidget(button);
        return button;
    };

    listButton_ = createButton(QStringLiteral("便签列表"));
    themeButton_ = createButton(QStringLiteral("主题配色"));
    settingsButton_ = createButton(QStringLiteral("设置"));
    securityButton_ = createButton(QStringLiteral("启用加密"));
    securityButton_->hide();
    newButton_ = createButton(QStringLiteral("新建便签"));
    closeButton_ = createButton(QStringLiteral("关闭当前便签"));

    connect(listButton_, &QToolButton::clicked, this, &TitleBar::listRequested);
    connect(themeButton_, &QToolButton::clicked, this, &TitleBar::themeRequested);
    connect(settingsButton_, &QToolButton::clicked, this, &TitleBar::settingsRequested);
    connect(securityButton_, &QToolButton::clicked, this, &TitleBar::securityRequested);
    connect(newButton_, &QToolButton::clicked, this, &TitleBar::newNoteRequested);
    connect(closeButton_, &QToolButton::clicked, this, &TitleBar::closeRequested);
    connect(titleEdit_, &QLineEdit::returnPressed, this, [this]() {
        finishEdit(true);
    });

    setTitle(newTimestampTitle());
    setTheme(ThemeCatalog::themeById(QStringLiteral("paper")));
}

void TitleBar::setTitle(const QString &title)
{
    stableTitle_ = title;
    titleLabel_->setText(title);
    if (titleStack_->currentWidget() == titleEdit_) {
        titleEdit_->setText(title);
    }
}

QString TitleBar::title() const
{
    return stableTitle_;
}

void TitleBar::setTheme(const ThemeSpec &theme)
{
    theme_ = theme;
    updateIcons();

    setStyleSheet(QStringLiteral(R"(
QWidget#titleBar {
    background: %1;
}
QWidget#titleSurface {
    background: transparent;
}
QLabel#titleLabel {
    color: %2;
    padding: 0 8px;
    font-size: 14px;
    font-weight: 700;
}
QLineEdit#titleEdit {
    min-height: 28px;
    padding: 0 10px;
    border-radius: 9px;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QToolButton#titleButton {
    border: none;
    border-radius: 10px;
    background: transparent;
}
QToolButton#titleButton:hover {
    background: %5;
}
)")
                      .arg(theme.titleBarColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.editorColor.name(),
                           theme.hoverColor.name()));
}

void TitleBar::setSecurityState(bool encrypted, bool unlocked)
{
    encrypted_ = encrypted;
    unlocked_ = unlocked;

    if (!encrypted_) {
        securityButton_->setToolTip(QStringLiteral("启用加密"));
    } else if (unlocked_) {
        securityButton_->setToolTip(QStringLiteral("重新锁定当前便签"));
    } else {
        securityButton_->setToolTip(QStringLiteral("解锁当前便签"));
    }

    updateIcons();
}

void TitleBar::setTitleEditable(bool editable)
{
    titleEditable_ = editable;
}

QWidget *TitleBar::themeButtonWidget() const
{
    return themeButton_;
}

QWidget *TitleBar::listButtonWidget() const
{
    return listButton_;
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == titleEdit_) {
        if (event->type() == QEvent::FocusOut) {
            finishEdit(true);
            return false;
        }

        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishEdit(false);
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    if (watched != titleSurface_ && watched != titleLabel_) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonDblClick:
        if (titleEditable_) {
            beginEdit();
        }
        return true;
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            titlePressed_ = true;
            pressPosition_ = mouseEvent->globalPosition().toPoint();
        }
        return true;
    }
    case QEvent::MouseMove: {
        if (!titlePressed_) {
            return false;
        }

        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint delta = mouseEvent->globalPosition().toPoint() - pressPosition_;
        if (delta.manhattanLength() >= QApplication::startDragDistance()) {
            titlePressed_ = false;
            emit dragRequested();
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        titlePressed_ = false;
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void TitleBar::beginEdit()
{
    titleEdit_->setText(stableTitle_);
    titleStack_->setCurrentWidget(titleEdit_);
    titleEdit_->selectAll();
    titleEdit_->setFocus();
}

void TitleBar::finishEdit(bool accept)
{
    if (titleStack_->currentWidget() != titleEdit_) {
        return;
    }

    if (accept) {
        QString nextTitle = titleEdit_->text().trimmed();
        if (nextTitle.isEmpty()) {
            nextTitle = stableTitle_;
        }
        stableTitle_ = nextTitle;
        titleLabel_->setText(stableTitle_);
        emit titleEdited(stableTitle_);
    } else {
        titleEdit_->setText(stableTitle_);
    }

    titleStack_->setCurrentWidget(titleLabel_);
}

void TitleBar::updateIcons()
{
    listButton_->setIcon(IconFactory::listIcon(theme_.textColor));
    themeButton_->setIcon(IconFactory::paletteIcon(theme_.textColor));
    settingsButton_->setIcon(IconFactory::settingsIcon(theme_.textColor));
    securityButton_->setIcon(encrypted_ && unlocked_ ? IconFactory::lockOpenIcon(theme_.textColor)
                                                     : IconFactory::lockClosedIcon(theme_.textColor));
    newButton_->setIcon(IconFactory::plusIcon(theme_.textColor));
    closeButton_->setIcon(IconFactory::closeIcon(theme_.textColor));
}
