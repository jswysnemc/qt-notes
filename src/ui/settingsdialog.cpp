#include "ui/settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QFrame>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionComboBox>
#include <QVBoxLayout>

namespace {

QColor mixedColor(const QColor &base, const QColor &overlay, qreal overlayRatio)
{
    const qreal clampedRatio = qBound(0.0, overlayRatio, 1.0);
    const auto blendChannel = [clampedRatio](int baseValue, int overlayValue) {
        return qRound((baseValue * (1.0 - clampedRatio)) + (overlayValue * clampedRatio));
    };
    return QColor(blendChannel(base.red(), overlay.red()),
                  blendChannel(base.green(), overlay.green()),
                  blendChannel(base.blue(), overlay.blue()));
}

class ModernComboPopupDelegate : public QStyledItemDelegate
{
public:
    explicit ModernComboPopupDelegate(const ThemeSpec &theme, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , theme_(theme)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 36));
        return size;
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRectF card = option.rect.adjusted(6, 2, -6, -2);
        QColor background = Qt::transparent;
        if (option.state.testFlag(QStyle::State_Selected)) {
            background = mixedColor(theme_.hoverColor, theme_.surfaceColor, 0.26);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            background = mixedColor(theme_.hoverColor, theme_.surfaceColor, 0.16);
        }

        if (background.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(background);
            painter->drawRoundedRect(card, 9.0, 9.0);
        }

        const QRect textRect = card.adjusted(12, 0, -28, 0).toRect();
        const QString text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideRight,
                                                           textRect.width());
        painter->setPen(option.state.testFlag(QStyle::State_Enabled) ? theme_.textColor
                                                                     : theme_.mutedTextColor);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme_.accentColor);
            painter->drawEllipse(QRectF(card.right() - 14.0, card.center().y() - 3.0, 6.0, 6.0));
        }

        painter->restore();
    }

private:
    ThemeSpec theme_;
};

class ModernComboBox : public QComboBox
{
public:
    explicit ModernComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(40);
        setMaxVisibleItems(8);
    }

    void setTheme(const ThemeSpec &theme)
    {
        theme_ = theme;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QStyleOptionComboBox option;
        initStyleOption(&option);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const bool enabled = option.state.testFlag(QStyle::State_Enabled);
        const bool hovered = underMouse();
        const bool active = hasFocus() || (view() != nullptr && view()->isVisible());

        QColor panelColor = theme_.editorColor;
        QColor borderColor = theme_.borderColor;
        QColor actionColor = mixedColor(theme_.hoverColor, theme_.surfaceColor, 0.28);
        QColor textColor = enabled ? theme_.textColor : theme_.mutedTextColor;

        if (hovered) {
            panelColor = mixedColor(theme_.editorColor, theme_.surfaceColor, 0.22);
            borderColor = mixedColor(theme_.borderColor, theme_.accentColor, 0.38);
            actionColor = mixedColor(theme_.hoverColor, theme_.accentColor, 0.12);
        }
        if (active) {
            panelColor = mixedColor(theme_.editorColor, theme_.surfaceColor, 0.16);
            borderColor = mixedColor(theme_.borderColor, theme_.accentColor, 0.68);
            actionColor = mixedColor(theme_.hoverColor, theme_.accentColor, 0.24);
        }
        if (!enabled) {
            panelColor = mixedColor(theme_.editorColor, theme_.surfaceColor, 0.42);
            borderColor = mixedColor(theme_.borderColor, theme_.surfaceColor, 0.55);
            actionColor = mixedColor(theme_.editorColor, theme_.surfaceColor, 0.18);
        }

        const QRectF frameRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(borderColor, active ? 1.6 : 1.0));
        painter.setBrush(panelColor);
        painter.drawRoundedRect(frameRect, 12.0, 12.0);

        if (active) {
            QColor haloColor = theme_.accentColor;
            haloColor.setAlpha(44);
            painter.setPen(QPen(haloColor, 3.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(frameRect.adjusted(1.5, 1.5, -1.5, -1.5), 10.0, 10.0);
        }

        const QRectF actionRect(frameRect.right() - 36.0,
                                frameRect.top() + 4.0,
                                32.0,
                                frameRect.height() - 8.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(actionColor);
        painter.drawRoundedRect(actionRect, 9.0, 9.0);

        const QRect textRect = rect().adjusted(14, 0, -48, 0);
        const QString text = fontMetrics().elidedText(option.currentText, Qt::ElideRight, textRect.width());
        painter.setPen(textColor);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        const QColor arrowColor =
            enabled ? mixedColor(theme_.textColor, theme_.accentColor, active ? 0.16 : 0.0)
                    : theme_.mutedTextColor;
        painter.setPen(QPen(arrowColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center = actionRect.center();
        painter.drawLine(center + QPointF(-4.5, -1.5), center + QPointF(0.0, 2.5));
        painter.drawLine(center + QPointF(0.0, 2.5), center + QPointF(4.5, -1.5));
    }

    void enterEvent(QEnterEvent *event) override
    {
        QComboBox::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QComboBox::leaveEvent(event);
        update();
    }

    void focusInEvent(QFocusEvent *event) override
    {
        QComboBox::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QComboBox::focusOutEvent(event);
        update();
    }

    void showPopup() override
    {
        QComboBox::showPopup();
        update();
    }

    void hidePopup() override
    {
        QComboBox::hidePopup();
        update();
    }

private:
    ThemeSpec theme_ = ThemeCatalog::themeById(QStringLiteral("paper"));
};

void applyPopupListStyle(QListView *view, const ThemeSpec &theme)
{
    if (view == nullptr) {
        return;
    }

    view->setFrameShape(QFrame::NoFrame);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setMouseTracking(true);
    view->setSpacing(2);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setItemDelegate(new ModernComboPopupDelegate(theme, view));
    view->setStyleSheet(QStringLiteral(R"(
QListView {
    padding: 6px;
    border: 1px solid %1;
    border-radius: 14px;
    background: %2;
    outline: none;
}
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 8px 4px 8px 0;
}
QScrollBar::handle:vertical {
    min-height: 24px;
    border-radius: 4px;
    background: %3;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
    border: none;
}
)")
                           .arg(mixedColor(theme.borderColor, theme.accentColor, 0.16).name(),
                                mixedColor(theme.editorColor, theme.surfaceColor, 0.08).name(),
                                mixedColor(theme.hoverColor, theme.borderColor, 0.42).name()));
}

void setupModernComboBox(QComboBox *combo, const ThemeSpec &theme)
{
    auto *modernCombo = dynamic_cast<ModernComboBox *>(combo);
    if (modernCombo == nullptr) {
        return;
    }

    modernCombo->setTheme(theme);
    modernCombo->setProperty("modernComboBox", true);

    auto *view = new QListView(modernCombo);
    applyPopupListStyle(view, theme);
    modernCombo->setView(view);
}

} // namespace

SettingsDialog::SettingsDialog(const QString &noteTitle,
                               bool wrapMode,
                               const QString &fontFamily,
                               int fontPointSize,
                               const QStringList &recentFonts,
                               SortMode sortMode,
                               StartupNoteMode startupNoteMode,
                               bool noteEncrypted,
                               bool encryptionCanBeDisabled,
                               bool encryptionPasswordsConfigured,
                               const ThemeSpec &theme,
                               QWidget *parent)
    : QDialog(parent)
    , noteTitle_(noteTitle)
{
    setWindowTitle(QStringLiteral("便签设置"));
    setModal(true);
    resize(740, 460);
    setMinimumSize(700, 420);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("便签设置"), this);
    titleLabel->setObjectName(QStringLiteral("settingsTitle"));
    rootLayout->addWidget(titleLabel);

    auto *bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(14);

    auto *routesList = new QListWidget(this);
    routesList->setObjectName(QStringLiteral("settingsRoutes"));
    routesList->setFrameShape(QFrame::NoFrame);
    routesList->setFixedWidth(138);
    routesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    routesList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    routesList->addItem(QStringLiteral("编辑"));
    routesList->addItem(QStringLiteral("启动"));
    routesList->addItem(QStringLiteral("安全"));
    routesList->addItem(QStringLiteral("删除"));
    bodyLayout->addWidget(routesList);

    auto *pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("settingsPages"));
    bodyLayout->addWidget(pages, 1);
    rootLayout->addLayout(bodyLayout, 1);

    auto createPageLayout = [this, pages](const QString &title, const QString &summary) {
        auto *page = new QWidget(pages);
        page->setObjectName(QStringLiteral("settingsPage"));
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(12);

        auto *pageTitle = new QLabel(title, page);
        pageTitle->setObjectName(QStringLiteral("settingsPageTitle"));
        layout->addWidget(pageTitle);

        if (!summary.isEmpty()) {
            auto *summaryLabel = new QLabel(summary, page);
            summaryLabel->setWordWrap(true);
            summaryLabel->setObjectName(QStringLiteral("settingsSummary"));
            layout->addWidget(summaryLabel);
        }

        pages->addWidget(page);
        return layout;
    };

    auto *editorPageLayout = createPageLayout(QStringLiteral("编辑"),
                                             QStringLiteral("调整当前便签的编辑行为和全局字体。"));

    auto *editorFormLayout = new QFormLayout();
    editorFormLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    editorFormLayout->setFormAlignment(Qt::AlignTop);
    editorFormLayout->setSpacing(10);

    wrapCheckBox_ = new QCheckBox(QStringLiteral("按窗口宽度自动换行"), this);
    wrapCheckBox_->setChecked(wrapMode);
    editorFormLayout->addRow(QStringLiteral("换行"), wrapCheckBox_);

    recentFontsCombo_ = new ModernComboBox(this);
    recentFontsCombo_->addItem(recentFonts.isEmpty() ? QStringLiteral("暂无最近字体")
                                                     : QStringLiteral("最近使用"));
    recentFontsCombo_->setItemData(0, QString(), Qt::UserRole);
    for (const QString &family : recentFonts) {
        recentFontsCombo_->addItem(family, family);
    }
    recentFontsCombo_->setEnabled(!recentFonts.isEmpty());
    editorFormLayout->addRow(QStringLiteral("最近字体"), recentFontsCombo_);

    fontFamilyCombo_ = new QFontComboBox(this);
    fontFamilyCombo_->setFontFilters(QFontComboBox::ScalableFonts);
    fontFamilyCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    fontFamilyCombo_->setMinimumContentsLength(14);
    fontFamilyCombo_->setMaxVisibleItems(12);
    editorFormLayout->addRow(QStringLiteral("系统字体"), fontFamilyCombo_);

    fontSizeSpinBox_ = new QSpinBox(this);
    fontSizeSpinBox_->setRange(10, 40);
    fontSizeSpinBox_->setValue(fontPointSize > 0 ? fontPointSize : 14);
    editorFormLayout->addRow(QStringLiteral("字号"), fontSizeSpinBox_);

    editorPageLayout->addLayout(editorFormLayout);
    editorPageLayout->addStretch();

    auto *startupPageLayout = createPageLayout(QStringLiteral("启动"),
                                              QStringLiteral("设置便签列表排序和应用启动时默认打开的便签。"));

    auto *startupFormLayout = new QFormLayout();
    startupFormLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    startupFormLayout->setFormAlignment(Qt::AlignTop);
    startupFormLayout->setSpacing(10);

    sortModeCombo_ = new ModernComboBox(this);
    sortModeCombo_->addItem(QStringLiteral("按最后编辑时间"), static_cast<int>(SortMode::LastEditedDesc));
    sortModeCombo_->addItem(QStringLiteral("按创建时间"), static_cast<int>(SortMode::CreatedDesc));
    sortModeCombo_->addItem(QStringLiteral("按标题"), static_cast<int>(SortMode::TitleAsc));
    sortModeCombo_->setCurrentIndex(sortModeCombo_->findData(static_cast<int>(sortMode)));
    startupFormLayout->addRow(QStringLiteral("列表排序"), sortModeCombo_);

    startupModeCombo_ = new ModernComboBox(this);
    startupModeCombo_->addItem(QStringLiteral("最后关闭"), static_cast<int>(StartupNoteMode::LastClosed));
    startupModeCombo_->addItem(QStringLiteral("最后编辑"), static_cast<int>(StartupNoteMode::LastEdited));
    startupModeCombo_->addItem(QStringLiteral("最后创建"), static_cast<int>(StartupNoteMode::LastCreated));
    startupModeCombo_->setCurrentIndex(
        startupModeCombo_->findData(static_cast<int>(startupNoteMode)));
    startupFormLayout->addRow(QStringLiteral("启动默认便签"), startupModeCombo_);

    setupModernComboBox(recentFontsCombo_, theme);
    setupModernComboBox(sortModeCombo_, theme);
    setupModernComboBox(startupModeCombo_, theme);

    startupPageLayout->addLayout(startupFormLayout);
    startupPageLayout->addStretch();

    auto *securityPageLayout = createPageLayout(QStringLiteral("安全"),
                                               QStringLiteral("管理当前便签加密状态和全局加密密码。"));
    auto *securityActionsLayout = new QVBoxLayout();
    securityActionsLayout->setContentsMargins(0, 0, 0, 0);
    securityActionsLayout->setSpacing(10);

    if (noteEncrypted) {
        securityActionButton_ = new QPushButton(encryptionCanBeDisabled
                                                    ? QStringLiteral("锁定当前便签")
                                                    : QStringLiteral("解锁当前便签"),
                                                this);
    } else {
        securityActionButton_ = new QPushButton(QStringLiteral("加密当前便签"), this);
    }
    securityActionsLayout->addWidget(securityActionButton_);

    if (noteEncrypted) {
        disableEncryptionButton_ = new QPushButton(QStringLiteral("取消当前加密"), this);
        disableEncryptionButton_->setEnabled(encryptionCanBeDisabled);
        if (!encryptionCanBeDisabled) {
            disableEncryptionButton_->setToolTip(
                QStringLiteral("请先解锁当前便签，再取消加密。"));
        }
        securityActionsLayout->addWidget(disableEncryptionButton_);
    }

    changeSimplePasswordButton_ = new QPushButton(QStringLiteral("修改短密码"), this);
    changeRecoveryPasswordButton_ = new QPushButton(QStringLiteral("修改长密码"), this);
    changeSimplePasswordButton_->setEnabled(encryptionPasswordsConfigured);
    changeRecoveryPasswordButton_->setEnabled(encryptionPasswordsConfigured);
    if (!encryptionPasswordsConfigured) {
        changeSimplePasswordButton_->setToolTip(QStringLiteral("首次启用加密后才能修改密码。"));
        changeRecoveryPasswordButton_->setToolTip(QStringLiteral("首次启用加密后才能修改密码。"));
    }
    securityActionsLayout->addWidget(changeSimplePasswordButton_);
    securityActionsLayout->addWidget(changeRecoveryPasswordButton_);
    securityPageLayout->addLayout(securityActionsLayout);
    securityPageLayout->addStretch();

    auto *dangerPageLayout = createPageLayout(QStringLiteral("删除"),
                                             QStringLiteral("删除当前便签。此操作需要再次确认。"));

    deleteButton_ = new QPushButton(QStringLiteral("删除当前便签"), this);
    deleteButton_->setObjectName(QStringLiteral("dangerButton"));
    dangerPageLayout->addWidget(deleteButton_);
    dangerPageLayout->addStretch();

    routesList->setCurrentRow(0);
    connect(routesList, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    auto *actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(10);
    actionsLayout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    actionsLayout->addWidget(buttons);
    rootLayout->addLayout(actionsLayout);

    connect(recentFontsCombo_,
            &QComboBox::currentIndexChanged,
            this,
            [this](int index) {
                const QString family = recentFontsCombo_->itemData(index, Qt::UserRole).toString();
                if (!family.isEmpty()) {
                    syncSelectedFont(family);
                }
            });

    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        const QString displayTitle = noteTitle_.trimmed().isEmpty() ? QStringLiteral("当前便签")
                                                                    : noteTitle_;
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this,
            QStringLiteral("确认删除"),
            QStringLiteral("删除后无法恢复：\n%1").arg(displayTitle),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes) {
            return;
        }

        deleteRequested_ = true;
        accept();
    });

    connect(securityActionButton_, &QPushButton::clicked, this, [this]() {
        securityActionRequested_ = true;
        accept();
    });

    connect(changeSimplePasswordButton_, &QPushButton::clicked, this, [this]() {
        changeSimplePasswordRequested_ = true;
        accept();
    });

    connect(changeRecoveryPasswordButton_, &QPushButton::clicked, this, [this]() {
        changeRecoveryPasswordRequested_ = true;
        accept();
    });

    if (disableEncryptionButton_ != nullptr) {
        connect(disableEncryptionButton_, &QPushButton::clicked, this, [this]() {
            const QString displayTitle = noteTitle_.trimmed().isEmpty() ? QStringLiteral("当前便签")
                                                                        : noteTitle_;
            const QMessageBox::StandardButton result = QMessageBox::warning(
                this,
                QStringLiteral("确认取消加密"),
                QStringLiteral("取消加密后，这条便签会转成普通便签，标题和正文将以明文写入 SQLite：\n%1")
                    .arg(displayTitle),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (result != QMessageBox::Yes) {
                return;
            }

            disableEncryptionRequested_ = true;
            accept();
        });
    }

    applyTheme(theme);
    syncSelectedFont(fontFamily.isEmpty() ? QApplication::font().family() : fontFamily);
}

bool SettingsDialog::wrapMode() const
{
    return wrapCheckBox_->isChecked();
}

QString SettingsDialog::fontFamily() const
{
    if (fontFamilyCombo_ != nullptr) {
        return fontFamilyCombo_->currentFont().family();
    }
    return QApplication::font().family();
}

int SettingsDialog::fontPointSize() const
{
    return fontSizeSpinBox_->value();
}

SortMode SettingsDialog::sortMode() const
{
    return static_cast<SortMode>(sortModeCombo_->currentData().toInt());
}

StartupNoteMode SettingsDialog::startupNoteMode() const
{
    return static_cast<StartupNoteMode>(startupModeCombo_->currentData().toInt());
}

bool SettingsDialog::securityActionRequested() const
{
    return securityActionRequested_;
}

bool SettingsDialog::disableEncryptionRequested() const
{
    return disableEncryptionRequested_;
}

bool SettingsDialog::changeSimplePasswordRequested() const
{
    return changeSimplePasswordRequested_;
}

bool SettingsDialog::changeRecoveryPasswordRequested() const
{
    return changeRecoveryPasswordRequested_;
}

bool SettingsDialog::deleteRequested() const
{
    return deleteRequested_;
}

void SettingsDialog::syncSelectedFont(const QString &fontFamily)
{
    if (fontFamilyCombo_ == nullptr) {
        return;
    }

    const QString target = fontFamily.trimmed();
    if (target.isEmpty()) {
        return;
    }

    const QSignalBlocker blocker(fontFamilyCombo_);
    fontFamilyCombo_->setCurrentFont(QFont(target));
}

void SettingsDialog::applyTheme(const ThemeSpec &theme)
{
    if (auto *combo = dynamic_cast<ModernComboBox *>(recentFontsCombo_); combo != nullptr) {
        combo->setTheme(theme);
    }
    if (auto *combo = dynamic_cast<ModernComboBox *>(sortModeCombo_); combo != nullptr) {
        combo->setTheme(theme);
    }
    if (auto *combo = dynamic_cast<ModernComboBox *>(startupModeCombo_); combo != nullptr) {
        combo->setTheme(theme);
    }

    const QString dialogStyle = QStringLiteral(R"(
QDialog {
    background: %1;
    color: %2;
    border: 1px solid %3;
    border-radius: 14px;
}
QLabel#settingsTitle {
    font-size: 15px;
    font-weight: 700;
    color: %2;
}
QLabel#settingsPageTitle {
    font-size: 14px;
    font-weight: 700;
    color: %2;
}
QLabel#settingsSummary {
    color: %8;
}
QLabel,
QCheckBox {
    color: %2;
}
QListWidget#settingsRoutes {
    padding: 6px;
    border: 1px solid %3;
    border-radius: 14px;
    background: %4;
    outline: none;
}
QListWidget#settingsRoutes::item {
    min-height: 34px;
    padding: 0 12px;
    border-radius: 10px;
    color: %8;
}
QListWidget#settingsRoutes::item:hover {
    background: %5;
    color: %2;
}
QListWidget#settingsRoutes::item:selected {
    background: %6;
    color: %2;
    font-weight: 700;
}
QStackedWidget#settingsPages {
    border: 1px solid %3;
    border-radius: 14px;
    background: %4;
}
QSpinBox {
    min-height: 32px;
    padding: 0 12px;
    border-radius: 10px;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QSpinBox:hover {
    border-color: %5;
}
QSpinBox:focus {
    border-color: %6;
    background: %7;
}
QComboBox[modernComboBox="true"] {
    min-height: 40px;
    padding: 0;
    border: none;
    background: transparent;
}
QComboBox[modernComboBox="true"]:focus {
    outline: none;
}
QSpinBox::up-button,
QSpinBox::down-button {
    border: none;
    width: 22px;
}
QSpinBox::up-arrow,
QSpinBox::down-arrow {
    width: 9px;
    height: 9px;
}
QPushButton {
    min-height: 34px;
    padding: 0 14px;
    border-radius: 10px;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QPushButton:hover {
    background: %5;
}
QPushButton:disabled {
    background: %1;
    border-color: %3;
    color: %3;
}
QPushButton#dangerButton {
    background: #B94A48;
    border-color: #A23E3C;
    color: #FFF8F8;
}
QPushButton#dangerButton:hover {
    background: #C95A58;
}
)")
                                    .arg(theme.surfaceColor.name())
                                    .arg(theme.textColor.name())
                                    .arg(theme.borderColor.name())
                                    .arg(theme.editorColor.name())
                                    .arg(mixedColor(theme.hoverColor, theme.borderColor, 0.42).name())
                                    .arg(mixedColor(theme.accentColor, theme.borderColor, 0.58).name())
                                    .arg(mixedColor(theme.editorColor, theme.surfaceColor, 0.16).name())
                                    .arg(theme.mutedTextColor.name());
    setStyleSheet(dialogStyle);
}
