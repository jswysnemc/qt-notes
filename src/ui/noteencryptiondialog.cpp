#include "ui/noteencryptiondialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

#include "security/notecrypto.h"

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

void stylePasswordEdit(QLineEdit *edit)
{
    if (edit == nullptr) {
        return;
    }

    edit->setEchoMode(QLineEdit::Password);
    edit->setClearButtonEnabled(true);
}

} // namespace

EncryptNoteDialog::EncryptNoteDialog(const QString &noteTitle,
                                     EncryptNoteDialogMode mode,
                                     const ThemeSpec &theme,
                                     QWidget *parent)
    : QDialog(parent)
    , mode_(mode)
{
    setWindowTitle(mode_ == EncryptNoteDialogMode::SetupGlobalPasswords
                       ? QStringLiteral("设置全局加密密码")
                       : QStringLiteral("输入全局加密密码"));
    setModal(true);
    resize(620, mode_ == EncryptNoteDialogMode::SetupGlobalPasswords ? 390 : 310);
    setMinimumWidth(620);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 22, 24, 22);
    rootLayout->setSpacing(18);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setObjectName(QStringLiteral("summaryLabel"));
    const QString displayTitle = noteTitle.trimmed().isEmpty() ? QStringLiteral("未命名便签")
                                                               : noteTitle;
    if (mode_ == EncryptNoteDialogMode::SetupGlobalPasswords) {
        summaryLabel_->setText(
            QStringLiteral("当前便签：%1\n首次启用加密需要先设置一套全局密码。后续加密便签都会共用这套简单密码和复杂恢复密码。")
                .arg(displayTitle));
    } else {
        summaryLabel_->setText(
            QStringLiteral("当前便签：%1\n该应用已经设置过全局加密密码。输入同一套简单密码和复杂恢复密码后，即可对当前便签启用加密。")
                .arg(displayTitle));
    }
    rootLayout->addWidget(summaryLabel_);

    auto *formLayout = new QFormLayout();
    formLayout->setHorizontalSpacing(18);
    formLayout->setVerticalSpacing(14);

    simplePasswordEdit_ = new QLineEdit(this);
    simplePasswordConfirmEdit_ = new QLineEdit(this);
    recoveryPasswordEdit_ = new QLineEdit(this);
    recoveryPasswordConfirmEdit_ = new QLineEdit(this);

    stylePasswordEdit(simplePasswordEdit_);
    stylePasswordEdit(simplePasswordConfirmEdit_);
    stylePasswordEdit(recoveryPasswordEdit_);
    stylePasswordEdit(recoveryPasswordConfirmEdit_);

    formLayout->addRow(QStringLiteral("简单密码"), simplePasswordEdit_);
    formLayout->addRow(QStringLiteral("复杂恢复密码"), recoveryPasswordEdit_);
    if (mode_ == EncryptNoteDialogMode::SetupGlobalPasswords) {
        simplePasswordEdit_->setPlaceholderText(QStringLiteral("至少 4 个字符"));
        recoveryPasswordEdit_->setPlaceholderText(
            QStringLiteral("至少 12 个字符，建议包含大小写、数字和符号"));
        formLayout->addRow(QStringLiteral("确认简单密码"), simplePasswordConfirmEdit_);
        formLayout->addRow(QStringLiteral("确认恢复密码"), recoveryPasswordConfirmEdit_);
    } else {
        simplePasswordEdit_->setPlaceholderText(QStringLiteral("输入已设置的全局简单密码"));
        recoveryPasswordEdit_->setPlaceholderText(QStringLiteral("输入已设置的全局复杂恢复密码"));
        simplePasswordConfirmEdit_->hide();
        recoveryPasswordConfirmEdit_->hide();
    }
    rootLayout->addLayout(formLayout);

    auto *hintLabel = new QLabel(
        QStringLiteral("安全限制：含图片附件的便签暂不支持启用加密，避免附件明文落盘。"), this);
    hintLabel->setWordWrap(true);
    hintLabel->setObjectName(QStringLiteral("hintLabel"));
    rootLayout->addWidget(hintLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &EncryptNoteDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    applyTheme(theme);
}

QString EncryptNoteDialog::simplePassword() const
{
    return simplePasswordEdit_->text();
}

QString EncryptNoteDialog::recoveryPassword() const
{
    return recoveryPasswordEdit_->text();
}

void EncryptNoteDialog::accept()
{
    const QString simple = simplePasswordEdit_->text();
    const QString recovery = recoveryPasswordEdit_->text();

    if (mode_ == EncryptNoteDialogMode::EnterGlobalPasswords) {
        if (simple.isEmpty() || recovery.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("密码为空"), QStringLiteral("请输入完整的全局密码。"));
            return;
        }
        QDialog::accept();
        return;
    }

    const QString simpleConfirm = simplePasswordConfirmEdit_->text();
    const QString recoveryConfirm = recoveryPasswordConfirmEdit_->text();
    if (simple != simpleConfirm) {
        QMessageBox::warning(this, QStringLiteral("密码不一致"), QStringLiteral("简单密码两次输入不一致。"));
        return;
    }
    if (recovery != recoveryConfirm) {
        QMessageBox::warning(this,
                             QStringLiteral("密码不一致"),
                             QStringLiteral("复杂恢复密码两次输入不一致。"));
        return;
    }
    if (simple == recovery) {
        QMessageBox::warning(this,
                             QStringLiteral("密码不合规"),
                             QStringLiteral("简单密码和复杂恢复密码不能相同。"));
        return;
    }

    QString errorMessage;
    if (!NoteCrypto::looksAcceptableSimplePassword(simple, &errorMessage)
        || !NoteCrypto::looksStrongRecoveryPassword(recovery, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("密码不合规"), errorMessage);
        return;
    }

    QDialog::accept();
}

void EncryptNoteDialog::applyTheme(const ThemeSpec &theme)
{
    setStyleSheet(QStringLiteral(R"(
QDialog {
    background: %1;
    color: %2;
    border: 1px solid %3;
    border-radius: 0;
}
QLabel#summaryLabel {
    color: %2;
    font-weight: 600;
}
QLabel#hintLabel {
    color: %4;
}
QLabel {
    color: %2;
}
QLineEdit {
    min-height: 38px;
    padding: 0 14px;
    border-radius: 0;
    border: 1px solid %3;
    background: %5;
    color: %2;
}
QLineEdit:focus {
    border-color: %6;
    background: %7;
}
QPushButton {
    min-height: 36px;
    padding: 0 18px;
    border-radius: 0;
    border: 1px solid %3;
    background: %5;
    color: %2;
}
QPushButton:hover {
    background: %8;
}
)")
                      .arg(theme.surfaceColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.mutedTextColor.name(),
                           theme.editorColor.name(),
                           mixedColor(theme.accentColor, theme.borderColor, 0.58).name(),
                           mixedColor(theme.editorColor, theme.surfaceColor, 0.16).name(),
                           mixedColor(theme.hoverColor, theme.borderColor, 0.42).name()));
}

UnlockNoteDialog::UnlockNoteDialog(bool recoveryPasswordRequired,
                                   int remainingSimpleAttempts,
                                   const ThemeSpec &theme,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(recoveryPasswordRequired ? QStringLiteral("输入复杂恢复密码")
                                            : QStringLiteral("输入简单密码"));
    setModal(true);
    resize(560, 250);
    setMinimumWidth(560);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 22, 24, 22);
    rootLayout->setSpacing(18);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setObjectName(QStringLiteral("summaryLabel"));
    if (recoveryPasswordRequired) {
        summaryLabel_->setText(
            QStringLiteral("当前便签已锁定。连续输错 3 次后，必须输入复杂恢复密码才能重新解锁。"));
    } else {
        summaryLabel_->setText(QStringLiteral("当前便签已加密。输入简单密码后可临时解锁。还剩 %1 次机会。")
                                   .arg(qMax(0, remainingSimpleAttempts)));
    }
    rootLayout->addWidget(summaryLabel_);

    passwordEdit_ = new QLineEdit(this);
    stylePasswordEdit(passwordEdit_);
    passwordEdit_->setPlaceholderText(recoveryPasswordRequired ? QStringLiteral("输入复杂恢复密码")
                                                               : QStringLiteral("输入简单密码"));
    passwordEdit_->setFocus();
    rootLayout->addWidget(passwordEdit_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &UnlockNoteDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    applyTheme(theme);
}

QString UnlockNoteDialog::password() const
{
    return passwordEdit_->text();
}

void UnlockNoteDialog::accept()
{
    if (passwordEdit_->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("密码为空"), QStringLiteral("请输入密码。"));
        return;
    }

    QDialog::accept();
}

void UnlockNoteDialog::applyTheme(const ThemeSpec &theme)
{
    setStyleSheet(QStringLiteral(R"(
QDialog {
    background: %1;
    color: %2;
    border: 1px solid %3;
    border-radius: 0;
}
QLabel#summaryLabel {
    color: %2;
    font-weight: 600;
}
QLineEdit {
    min-height: 40px;
    padding: 0 14px;
    border-radius: 0;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QLineEdit:focus {
    border-color: %5;
    background: %6;
}
QPushButton {
    min-height: 36px;
    padding: 0 18px;
    border-radius: 0;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QPushButton:hover {
    background: %7;
}
)")
                      .arg(theme.surfaceColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.editorColor.name(),
                           mixedColor(theme.accentColor, theme.borderColor, 0.58).name(),
                           mixedColor(theme.editorColor, theme.surfaceColor, 0.16).name(),
                           mixedColor(theme.hoverColor, theme.borderColor, 0.42).name()));
}

ChangeEncryptionPasswordDialog::ChangeEncryptionPasswordDialog(ChangeEncryptionPasswordMode mode,
                                                               const ThemeSpec &theme,
                                                               QWidget *parent)
    : QDialog(parent)
    , mode_(mode)
{
    const bool simpleMode = mode_ == ChangeEncryptionPasswordMode::SimplePassword;
    setWindowTitle(simpleMode ? QStringLiteral("修改短密码") : QStringLiteral("修改长密码"));
    setModal(true);
    resize(620, 390);
    setMinimumWidth(620);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 22, 24, 22);
    rootLayout->setSpacing(18);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setObjectName(QStringLiteral("summaryLabel"));
    summaryLabel_->setText(simpleMode
                               ? QStringLiteral("修改短密码会同时更新所有加密便签的短密码密钥封装。")
                               : QStringLiteral("修改长密码会同时更新所有加密便签的长密码密钥封装。"));
    rootLayout->addWidget(summaryLabel_);

    auto *formLayout = new QFormLayout();
    formLayout->setHorizontalSpacing(18);
    formLayout->setVerticalSpacing(14);

    currentSimplePasswordEdit_ = new QLineEdit(this);
    currentRecoveryPasswordEdit_ = new QLineEdit(this);
    newPasswordEdit_ = new QLineEdit(this);
    newPasswordConfirmEdit_ = new QLineEdit(this);

    stylePasswordEdit(currentSimplePasswordEdit_);
    stylePasswordEdit(currentRecoveryPasswordEdit_);
    stylePasswordEdit(newPasswordEdit_);
    stylePasswordEdit(newPasswordConfirmEdit_);

    currentSimplePasswordEdit_->setPlaceholderText(QStringLiteral("输入当前短密码"));
    currentRecoveryPasswordEdit_->setPlaceholderText(QStringLiteral("输入当前长密码"));
    newPasswordEdit_->setPlaceholderText(simpleMode ? QStringLiteral("至少 4 个字符")
                                                    : QStringLiteral("至少 12 个字符"));
    newPasswordConfirmEdit_->setPlaceholderText(QStringLiteral("再次输入新密码"));

    formLayout->addRow(QStringLiteral("当前短密码"), currentSimplePasswordEdit_);
    formLayout->addRow(QStringLiteral("当前长密码"), currentRecoveryPasswordEdit_);
    formLayout->addRow(simpleMode ? QStringLiteral("新短密码") : QStringLiteral("新长密码"),
                       newPasswordEdit_);
    formLayout->addRow(QStringLiteral("确认新密码"), newPasswordConfirmEdit_);
    rootLayout->addLayout(formLayout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ChangeEncryptionPasswordDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    applyTheme(theme);
}

QString ChangeEncryptionPasswordDialog::currentSimplePassword() const
{
    return currentSimplePasswordEdit_->text();
}

QString ChangeEncryptionPasswordDialog::currentRecoveryPassword() const
{
    return currentRecoveryPasswordEdit_->text();
}

QString ChangeEncryptionPasswordDialog::newPassword() const
{
    return newPasswordEdit_->text();
}

void ChangeEncryptionPasswordDialog::accept()
{
    const QString currentSimple = currentSimplePasswordEdit_->text();
    const QString currentRecovery = currentRecoveryPasswordEdit_->text();
    const QString nextPassword = newPasswordEdit_->text();
    const QString nextPasswordConfirm = newPasswordConfirmEdit_->text();

    if (currentSimple.isEmpty() || currentRecovery.isEmpty() || nextPassword.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("密码为空"), QStringLiteral("请输入完整的密码。"));
        return;
    }
    if (nextPassword != nextPasswordConfirm) {
        QMessageBox::warning(this, QStringLiteral("密码不一致"), QStringLiteral("新密码两次输入不一致。"));
        return;
    }
    if (mode_ == ChangeEncryptionPasswordMode::SimplePassword && nextPassword == currentRecovery) {
        QMessageBox::warning(this,
                             QStringLiteral("密码不合规"),
                             QStringLiteral("短密码和长密码不能相同。"));
        return;
    }
    if (mode_ == ChangeEncryptionPasswordMode::RecoveryPassword && nextPassword == currentSimple) {
        QMessageBox::warning(this,
                             QStringLiteral("密码不合规"),
                             QStringLiteral("短密码和长密码不能相同。"));
        return;
    }

    QString errorMessage;
    const bool valid = mode_ == ChangeEncryptionPasswordMode::SimplePassword
                           ? NoteCrypto::looksAcceptableSimplePassword(nextPassword, &errorMessage)
                           : NoteCrypto::looksStrongRecoveryPassword(nextPassword, &errorMessage);
    if (!valid) {
        QMessageBox::warning(this, QStringLiteral("密码不合规"), errorMessage);
        return;
    }

    QDialog::accept();
}

void ChangeEncryptionPasswordDialog::applyTheme(const ThemeSpec &theme)
{
    setStyleSheet(QStringLiteral(R"(
QDialog {
    background: %1;
    color: %2;
    border: 1px solid %3;
    border-radius: 0;
}
QLabel#summaryLabel {
    color: %2;
    font-weight: 600;
}
QLabel {
    color: %2;
}
QLineEdit {
    min-height: 38px;
    padding: 0 14px;
    border-radius: 0;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QLineEdit:focus {
    border-color: %5;
    background: %6;
}
QPushButton {
    min-height: 36px;
    padding: 0 18px;
    border-radius: 0;
    border: 1px solid %3;
    background: %4;
    color: %2;
}
QPushButton:hover {
    background: %7;
}
)")
                      .arg(theme.surfaceColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.editorColor.name(),
                           mixedColor(theme.accentColor, theme.borderColor, 0.58).name(),
                           mixedColor(theme.editorColor, theme.surfaceColor, 0.16).name(),
                           mixedColor(theme.hoverColor, theme.borderColor, 0.42).name()));
}
