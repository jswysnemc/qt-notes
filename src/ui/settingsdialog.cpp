#include "ui/settingsdialog.h"

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QStringList &cachedFontFamilies()
{
    static const QStringList families = []() {
        QFontDatabase database;
        QStringList result = database.families();
        result.removeDuplicates();
        std::sort(result.begin(), result.end(), [](const QString &left, const QString &right) {
            return left.localeAwareCompare(right) < 0;
        });
        return result;
    }();
    return families;
}

} // namespace

SettingsDialog::SettingsDialog(const QString &noteTitle,
                               bool wrapMode,
                               const QString &fontFamily,
                               int fontPointSize,
                               const QStringList &recentFonts,
                               SortMode sortMode,
                               StartupNoteMode startupNoteMode,
                               const ThemeSpec &theme,
                               QWidget *parent)
    : QDialog(parent)
    , noteTitle_(noteTitle)
{
    setWindowTitle(QStringLiteral("便签设置"));
    setModal(true);
    resize(560, 460);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("当前便签"), this);
    titleLabel->setObjectName(QStringLiteral("settingsTitle"));
    rootLayout->addWidget(titleLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignTop);
    formLayout->setSpacing(10);

    wrapCheckBox_ = new QCheckBox(QStringLiteral("按窗口宽度自动换行"), this);
    wrapCheckBox_->setChecked(wrapMode);
    formLayout->addRow(QStringLiteral("换行"), wrapCheckBox_);

    recentFontsCombo_ = new QComboBox(this);
    recentFontsCombo_->addItem(recentFonts.isEmpty() ? QStringLiteral("暂无最近字体")
                                                     : QStringLiteral("最近使用"));
    recentFontsCombo_->setItemData(0, QString(), Qt::UserRole);
    for (const QString &family : recentFonts) {
        recentFontsCombo_->addItem(family, family);
    }
    recentFontsCombo_->setEnabled(!recentFonts.isEmpty());
    formLayout->addRow(QStringLiteral("最近字体"), recentFontsCombo_);

    auto *fontColumn = new QWidget(this);
    auto *fontColumnLayout = new QVBoxLayout(fontColumn);
    fontColumnLayout->setContentsMargins(0, 0, 0, 0);
    fontColumnLayout->setSpacing(8);

    fontSearchEdit_ = new QLineEdit(fontColumn);
    fontSearchEdit_->setPlaceholderText(QStringLiteral("输入字体名称搜索"));
    fontColumnLayout->addWidget(fontSearchEdit_);

    fontListWidget_ = new QListWidget(fontColumn);
    fontListWidget_->setMinimumHeight(150);
    fontListWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
    fontListWidget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    fontListWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fontListWidget_->setEnabled(false);
    fontListWidget_->addItem(QStringLiteral("正在加载系统字体..."));
    fontColumnLayout->addWidget(fontListWidget_);

    formLayout->addRow(QStringLiteral("系统字体"), fontColumn);

    fontSizeSpinBox_ = new QSpinBox(this);
    fontSizeSpinBox_->setRange(10, 40);
    fontSizeSpinBox_->setValue(fontPointSize > 0 ? fontPointSize : 14);
    formLayout->addRow(QStringLiteral("字号"), fontSizeSpinBox_);

    sortModeCombo_ = new QComboBox(this);
    sortModeCombo_->addItem(QStringLiteral("按最后编辑时间"), static_cast<int>(SortMode::LastEditedDesc));
    sortModeCombo_->addItem(QStringLiteral("按创建时间"), static_cast<int>(SortMode::CreatedDesc));
    sortModeCombo_->addItem(QStringLiteral("按标题"), static_cast<int>(SortMode::TitleAsc));
    sortModeCombo_->setCurrentIndex(sortModeCombo_->findData(static_cast<int>(sortMode)));
    formLayout->addRow(QStringLiteral("列表排序"), sortModeCombo_);

    startupModeCombo_ = new QComboBox(this);
    startupModeCombo_->addItem(QStringLiteral("最后关闭"), static_cast<int>(StartupNoteMode::LastClosed));
    startupModeCombo_->addItem(QStringLiteral("最后编辑"), static_cast<int>(StartupNoteMode::LastEdited));
    startupModeCombo_->addItem(QStringLiteral("最后创建"), static_cast<int>(StartupNoteMode::LastCreated));
    startupModeCombo_->setCurrentIndex(
        startupModeCombo_->findData(static_cast<int>(startupNoteMode)));
    formLayout->addRow(QStringLiteral("启动默认便签"), startupModeCombo_);

    rootLayout->addLayout(formLayout);
    rootLayout->addStretch();

    auto *actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(10);

    deleteButton_ = new QPushButton(QStringLiteral("删除当前便签"), this);
    deleteButton_->setObjectName(QStringLiteral("dangerButton"));
    actionsLayout->addWidget(deleteButton_);
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

    connect(fontSearchEdit_, &QLineEdit::textChanged, this, &SettingsDialog::filterFontFamilies);

    connect(fontListWidget_, &QListWidget::currentTextChanged, this, [this](const QString &text) {
        if (fontSearchEdit_->text().trimmed() == text.trimmed()) {
            return;
        }
        const QSignalBlocker blocker(fontSearchEdit_);
        fontSearchEdit_->setText(text);
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

    applyTheme(theme);

    QTimer::singleShot(0, this, [this, fontFamily]() {
        populateFontFamilies(fontFamily);
    });
}

bool SettingsDialog::wrapMode() const
{
    return wrapCheckBox_->isChecked();
}

QString SettingsDialog::fontFamily() const
{
    if (QListWidgetItem *item = fontListWidget_->currentItem(); item != nullptr) {
        return item->text().trimmed();
    }
    return fontSearchEdit_->text().trimmed();
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

bool SettingsDialog::deleteRequested() const
{
    return deleteRequested_;
}

void SettingsDialog::populateFontFamilies(const QString &fontFamily)
{
    const QStringList &families = cachedFontFamilies();
    fontListWidget_->clear();
    fontListWidget_->addItems(families);
    fontListWidget_->setEnabled(true);
    syncSelectedFont(fontFamily.isEmpty() ? QApplication::font().family() : fontFamily);
}

void SettingsDialog::filterFontFamilies(const QString &text)
{
    const QString keyword = text.trimmed();
    for (int index = 0; index < fontListWidget_->count(); ++index) {
        QListWidgetItem *item = fontListWidget_->item(index);
        const bool matched = keyword.isEmpty()
                             || item->text().contains(keyword, Qt::CaseInsensitive);
        item->setHidden(!matched);
    }

    if (keyword.isEmpty()) {
        return;
    }

    for (int index = 0; index < fontListWidget_->count(); ++index) {
        QListWidgetItem *item = fontListWidget_->item(index);
        if (!item->isHidden()) {
            fontListWidget_->setCurrentItem(item);
            fontListWidget_->scrollToItem(item);
            return;
        }
    }
}

void SettingsDialog::syncSelectedFont(const QString &fontFamily)
{
    const QString target = fontFamily.trimmed();
    if (target.isEmpty()) {
        return;
    }

    {
        const QSignalBlocker blocker(fontSearchEdit_);
        fontSearchEdit_->setText(target);
    }

    for (int index = 0; index < fontListWidget_->count(); ++index) {
        QListWidgetItem *item = fontListWidget_->item(index);
        const bool matched = item->text().compare(target, Qt::CaseInsensitive) == 0;
        item->setHidden(false);
        if (matched) {
            fontListWidget_->setCurrentItem(item);
            fontListWidget_->scrollToItem(item);
            return;
        }
    }

    filterFontFamilies(target);
}

void SettingsDialog::applyTheme(const ThemeSpec &theme)
{
    setStyleSheet(QStringLiteral(R"(
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
QLabel,
QCheckBox {
    color: %2;
}
QComboBox,
QLineEdit,
QListWidget,
QSpinBox {
    min-height: 32px;
    padding: 0 10px;
    border-radius: 10px;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QSpinBox::up-button,
QSpinBox::down-button {
    border: none;
    width: 20px;
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
QPushButton#dangerButton {
    background: #B94A48;
    border-color: #A23E3C;
    color: #FFF8F8;
}
QPushButton#dangerButton:hover {
    background: #C95A58;
}
QListWidget {
    padding: 4px;
}
QListWidget::item {
    min-height: 26px;
    border-radius: 7px;
    padding: 0 8px;
}
QListWidget::item:selected {
    background: %5;
    color: %2;
}
)")
                      .arg(theme.surfaceColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.editorColor.name(),
                           theme.hoverColor.name()));
}
