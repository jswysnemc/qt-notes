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
                       ? tr("Set global encryption passwords")
                       : tr("Enter global encryption passwords"));
    setModal(true);
    resize(620, mode_ == EncryptNoteDialogMode::SetupGlobalPasswords ? 390 : 310);
    setMinimumWidth(620);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 22, 24, 22);
    rootLayout->setSpacing(18);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setObjectName(QStringLiteral("summaryLabel"));
    const QString displayTitle = noteTitle.trimmed().isEmpty() ? tr("Untitled note")
                                                               : noteTitle;
    if (mode_ == EncryptNoteDialogMode::SetupGlobalPasswords) {
        summaryLabel_->setText(
            tr("Current note: %1\nFirst-time encryption requires setting global passwords. All subsequent encrypted notes will share these simple and recovery passwords.")
                .arg(displayTitle));
    } else {
        summaryLabel_->setText(
            tr("Current note: %1\nGlobal encryption passwords have been set. Enter the same simple and recovery passwords to encrypt this note.")
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

    formLayout->addRow(tr("Simple password"), simplePasswordEdit_);
    formLayout->addRow(tr("Recovery password"), recoveryPasswordEdit_);
    if (mode_ == EncryptNoteDialogMode::SetupGlobalPasswords) {
        simplePasswordEdit_->setPlaceholderText(tr("At least 4 characters"));
        recoveryPasswordEdit_->setPlaceholderText(
            tr("At least 12 characters, recommended: uppercase, lowercase, digits, and symbols"));
        formLayout->addRow(tr("Confirm simple password"), simplePasswordConfirmEdit_);
        formLayout->addRow(tr("Confirm recovery password"), recoveryPasswordConfirmEdit_);
    } else {
        simplePasswordEdit_->setPlaceholderText(tr("Enter the configured global simple password"));
        recoveryPasswordEdit_->setPlaceholderText(tr("Enter the configured global recovery password"));
        simplePasswordConfirmEdit_->hide();
        recoveryPasswordConfirmEdit_->hide();
    }
    rootLayout->addLayout(formLayout);

    auto *hintLabel = new QLabel(
        tr("Security restriction: notes with image attachments cannot be encrypted to prevent plaintext exposure."), this);
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
            QMessageBox::warning(this, tr("Password is empty"), tr("Please enter the complete global passwords."));
            return;
        }
        QDialog::accept();
        return;
    }

    const QString simpleConfirm = simplePasswordConfirmEdit_->text();
    const QString recoveryConfirm = recoveryPasswordConfirmEdit_->text();
    if (simple != simpleConfirm) {
        QMessageBox::warning(this, tr("Passwords do not match"), tr("Simple password entries do not match."));
        return;
    }
    if (recovery != recoveryConfirm) {
        QMessageBox::warning(this,
                             tr("Passwords do not match"),
                             tr("Recovery password entries do not match."));
        return;
    }
    if (simple == recovery) {
        QMessageBox::warning(this,
                             tr("Invalid password"),
                             tr("Simple password and recovery password must be different."));
        return;
    }

    QString errorMessage;
    if (!NoteCrypto::looksAcceptableSimplePassword(simple, &errorMessage)
        || !NoteCrypto::looksStrongRecoveryPassword(recovery, &errorMessage)) {
        QMessageBox::warning(this, tr("Invalid password"), errorMessage);
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
    setWindowTitle(recoveryPasswordRequired ? tr("Enter recovery password")
                                            : tr("Enter simple password"));
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
            tr("This note is locked. After 3 failed attempts, the recovery password is required to unlock."));
    } else {
        summaryLabel_->setText(tr("This note is encrypted. Enter the simple password to unlock. %1 attempts remaining.")
                                   .arg(qMax(0, remainingSimpleAttempts)));
    }
    rootLayout->addWidget(summaryLabel_);

    passwordEdit_ = new QLineEdit(this);
    stylePasswordEdit(passwordEdit_);
    passwordEdit_->setPlaceholderText(recoveryPasswordRequired ? tr("Enter recovery password")
                                                               : tr("Enter simple password"));
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
        QMessageBox::warning(this, tr("Password is empty"), tr("Please enter a password."));
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
    setWindowTitle(simpleMode ? tr("Change simple password") : tr("Change recovery password"));
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
                               ? tr("Changing the simple password updates the key wrapping for all encrypted notes.")
                               : tr("Changing the recovery password updates the key wrapping for all encrypted notes."));
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

    currentSimplePasswordEdit_->setPlaceholderText(tr("Enter current simple password"));
    currentRecoveryPasswordEdit_->setPlaceholderText(tr("Enter current recovery password"));
    newPasswordEdit_->setPlaceholderText(simpleMode ? tr("At least 4 characters")
                                                    : tr("At least 12 characters"));
    newPasswordConfirmEdit_->setPlaceholderText(tr("Re-enter new password"));

    formLayout->addRow(tr("Current simple password"), currentSimplePasswordEdit_);
    formLayout->addRow(tr("Current recovery password"), currentRecoveryPasswordEdit_);
    formLayout->addRow(simpleMode ? tr("New simple password") : tr("New recovery password"),
                       newPasswordEdit_);
    formLayout->addRow(tr("Confirm new password"), newPasswordConfirmEdit_);
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
        QMessageBox::warning(this, tr("Password is empty"), tr("Please enter the complete passwords."));
        return;
    }
    if (nextPassword != nextPasswordConfirm) {
        QMessageBox::warning(this, tr("Passwords do not match"), tr("New password entries do not match."));
        return;
    }
    if (mode_ == ChangeEncryptionPasswordMode::SimplePassword && nextPassword == currentRecovery) {
        QMessageBox::warning(this,
                             tr("Invalid password"),
                             tr("Simple password and recovery password must be different."));
        return;
    }
    if (mode_ == ChangeEncryptionPasswordMode::RecoveryPassword && nextPassword == currentSimple) {
        QMessageBox::warning(this,
                             tr("Invalid password"),
                             tr("Simple password and recovery password must be different."));
        return;
    }

    QString errorMessage;
    const bool valid = mode_ == ChangeEncryptionPasswordMode::SimplePassword
                           ? NoteCrypto::looksAcceptableSimplePassword(nextPassword, &errorMessage)
                           : NoteCrypto::looksStrongRecoveryPassword(nextPassword, &errorMessage);
    if (!valid) {
        QMessageBox::warning(this, tr("Invalid password"), errorMessage);
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
