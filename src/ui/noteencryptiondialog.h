#pragma once

#include <QDialog>

#include "theme/themecatalog.h"

class QLabel;
class QLineEdit;

enum class EncryptNoteDialogMode {
    SetupGlobalPasswords = 0,
    EnterGlobalPasswords = 1,
};

class EncryptNoteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EncryptNoteDialog(const QString &noteTitle,
                               EncryptNoteDialogMode mode,
                               const ThemeSpec &theme,
                               QWidget *parent = nullptr);

    QString simplePassword() const;
    QString recoveryPassword() const;

protected:
    void accept() override;

private:
    void applyTheme(const ThemeSpec &theme);

    EncryptNoteDialogMode mode_ = EncryptNoteDialogMode::SetupGlobalPasswords;
    QLabel *summaryLabel_ = nullptr;
    QLineEdit *simplePasswordEdit_ = nullptr;
    QLineEdit *simplePasswordConfirmEdit_ = nullptr;
    QLineEdit *recoveryPasswordEdit_ = nullptr;
    QLineEdit *recoveryPasswordConfirmEdit_ = nullptr;
};

class UnlockNoteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnlockNoteDialog(bool recoveryPasswordRequired,
                              int remainingSimpleAttempts,
                              const ThemeSpec &theme,
                              QWidget *parent = nullptr);

    QString password() const;

protected:
    void accept() override;

private:
    void applyTheme(const ThemeSpec &theme);

    QLabel *summaryLabel_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
};

enum class ChangeEncryptionPasswordMode {
    SimplePassword = 0,
    RecoveryPassword = 1,
};

class ChangeEncryptionPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChangeEncryptionPasswordDialog(ChangeEncryptionPasswordMode mode,
                                            const ThemeSpec &theme,
                                            QWidget *parent = nullptr);

    QString currentSimplePassword() const;
    QString currentRecoveryPassword() const;
    QString newPassword() const;

protected:
    void accept() override;

private:
    void applyTheme(const ThemeSpec &theme);

    ChangeEncryptionPasswordMode mode_ = ChangeEncryptionPasswordMode::SimplePassword;
    QLabel *summaryLabel_ = nullptr;
    QLineEdit *currentSimplePasswordEdit_ = nullptr;
    QLineEdit *currentRecoveryPasswordEdit_ = nullptr;
    QLineEdit *newPasswordEdit_ = nullptr;
    QLineEdit *newPasswordConfirmEdit_ = nullptr;
};
