#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace NoteCrypto {

constexpr int kCryptoVersion = 1;
constexpr int kFailedAttemptsBeforeRecovery = 3;

enum class UnlockStatus {
    Success = 0,
    WrongPassword,
    RecoveryPasswordRequired,
    NotEncrypted,
    InvalidData,
};

struct WrappedKeyData {
    QByteArray ciphertext;
    QByteArray nonce;
    QByteArray salt;
    quint64 opsLimit = 0;
    quint64 memLimit = 0;
    int algorithm = 0;
};

struct EncryptedNoteData {
    int version = kCryptoVersion;
    QByteArray payloadCiphertext;
    QByteArray payloadNonce;
    WrappedKeyData simpleWrap;
    WrappedKeyData recoveryWrap;
};

struct UnlockedNoteData {
    QString title;
    QString content;
    QByteArray dataKey;
};

struct UnlockResult {
    UnlockStatus status = UnlockStatus::InvalidData;
    UnlockedNoteData note;
    QString errorMessage;
};

bool initialize(QString *errorMessage = nullptr);
bool looksAcceptableSimplePassword(const QString &password, QString *errorMessage = nullptr);
bool looksStrongRecoveryPassword(const QString &password, QString *errorMessage = nullptr);
bool createPasswordVerifier(const QString &password,
                            QByteArray *verifier,
                            QString *errorMessage = nullptr);
bool verifyPasswordVerifier(const QString &password, const QByteArray &verifier);

bool encryptForNewNote(qint64 noteId,
                       const QString &title,
                       const QString &content,
                       const QString &simplePassword,
                       const QString &recoveryPassword,
                       EncryptedNoteData *encrypted,
                       UnlockedNoteData *unlocked,
                       QString *errorMessage = nullptr);

bool encryptWithExistingKey(qint64 noteId,
                            const QString &title,
                            const QString &content,
                            const QByteArray &dataKey,
                            QByteArray *ciphertext,
                            QByteArray *nonce,
                            QString *errorMessage = nullptr);

bool rewrapDataKey(qint64 noteId,
                   const QString &oldPassword,
                   const QString &newPassword,
                   const WrappedKeyData &currentWrap,
                   bool useRecoveryPassword,
                   WrappedKeyData *nextWrap,
                   QString *errorMessage = nullptr);

bool encryptAttachmentBytes(qint64 noteId,
                            const QString &assetId,
                            const QByteArray &plaintext,
                            const QByteArray &dataKey,
                            QByteArray *ciphertext,
                            QByteArray *nonce,
                            QString *errorMessage = nullptr);

bool decryptAttachmentBytes(qint64 noteId,
                            const QString &assetId,
                            const QByteArray &ciphertext,
                            const QByteArray &nonce,
                            const QByteArray &dataKey,
                            QByteArray *plaintext,
                            QString *errorMessage = nullptr);

UnlockResult unlockWithPassword(qint64 noteId,
                                const EncryptedNoteData &encrypted,
                                const QString &password,
                                bool useRecoveryPassword);

void wipe(QByteArray *data);

} // namespace NoteCrypto
