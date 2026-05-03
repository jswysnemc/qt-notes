#include "security/notecrypto.h"

#include <limits>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <sodium.h>

namespace {

inline QString tr_c(const char *text)
{
    return QCoreApplication::translate("NoteCrypto", text);
}

} // anonymous namespace (forward declaration)

namespace {

constexpr quint64 kWrapOpsLimit = crypto_pwhash_OPSLIMIT_MODERATE;
constexpr quint64 kWrapMemLimit = crypto_pwhash_MEMLIMIT_MODERATE;

QByteArray associatedData(qint64 noteId, const char *purpose)
{
    return QByteArray(purpose) + ':' + QByteArray::number(noteId) + ":v1";
}

QByteArray attachmentAssociatedData(qint64 noteId, const QString &assetId)
{
    return QByteArray("asset:")
           + QByteArray::number(noteId)
           + ':'
           + assetId.toUtf8()
           + ":v1";
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validateWrapData(const NoteCrypto::WrappedKeyData &wrap)
{
    return wrap.ciphertext.size()
               == crypto_aead_xchacha20poly1305_ietf_KEYBYTES
                      + crypto_aead_xchacha20poly1305_ietf_ABYTES
           && wrap.nonce.size() == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
           && wrap.salt.size() == crypto_pwhash_SALTBYTES && wrap.opsLimit > 0
           && wrap.memLimit > 0 && wrap.algorithm == crypto_pwhash_ALG_ARGON2ID13;
}

bool validateEncryptedData(const NoteCrypto::EncryptedNoteData &encrypted)
{
    return encrypted.version == NoteCrypto::kCryptoVersion
           && encrypted.payloadCiphertext.size() >= crypto_aead_xchacha20poly1305_ietf_ABYTES
           && encrypted.payloadNonce.size() == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
           && validateWrapData(encrypted.simpleWrap) && validateWrapData(encrypted.recoveryWrap);
}

bool deriveKey(const QString &password,
               const QByteArray &salt,
               quint64 opsLimit,
               quint64 memLimit,
               int algorithm,
               QByteArray *derivedKey,
               QString *errorMessage)
{
    if (derivedKey == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing key derivation output"));
        return false;
    }

    QByteArray passwordUtf8 = password.toUtf8();
    QByteArray key(crypto_aead_xchacha20poly1305_ietf_KEYBYTES, Qt::Uninitialized);
    const bool okay =
        crypto_pwhash(reinterpret_cast<unsigned char *>(key.data()),
                      static_cast<unsigned long long>(key.size()),
                      passwordUtf8.constData(),
                      static_cast<unsigned long long>(passwordUtf8.size()),
                      reinterpret_cast<const unsigned char *>(salt.constData()),
                      static_cast<unsigned long long>(opsLimit),
                      static_cast<size_t>(memLimit),
                      algorithm)
        == 0;
    sodium_memzero(passwordUtf8.data(), static_cast<size_t>(passwordUtf8.size()));

    if (!okay) {
        NoteCrypto::wipe(&key);
        setError(errorMessage, tr_c("Failed to derive unlock key"));
        return false;
    }

    *derivedKey = key;
    return true;
}

bool encryptBytes(const QByteArray &plaintext,
                  const QByteArray &additionalData,
                  const QByteArray &key,
                  QByteArray *ciphertext,
                  QByteArray *nonce,
                  QString *errorMessage)
{
    if (ciphertext == nullptr || nonce == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing encryption output"));
        return false;
    }

    QByteArray generatedNonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, Qt::Uninitialized);
    randombytes_buf(generatedNonce.data(), static_cast<size_t>(generatedNonce.size()));

    QByteArray encrypted(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES,
                         Qt::Uninitialized);
    unsigned long long encryptedLength = 0;
    const int result =
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(encrypted.data()),
            &encryptedLength,
            reinterpret_cast<const unsigned char *>(plaintext.constData()),
            static_cast<unsigned long long>(plaintext.size()),
            additionalData.isEmpty()
                ? nullptr
                : reinterpret_cast<const unsigned char *>(additionalData.constData()),
            static_cast<unsigned long long>(additionalData.size()),
            nullptr,
            reinterpret_cast<const unsigned char *>(generatedNonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    if (result != 0) {
        setError(errorMessage, tr_c("Failed to generate note ciphertext"));
        return false;
    }

    encrypted.truncate(static_cast<int>(encryptedLength));
    *ciphertext = encrypted;
    *nonce = generatedNonce;
    return true;
}

bool decryptBytes(const QByteArray &ciphertext,
                  const QByteArray &additionalData,
                  const QByteArray &nonce,
                  const QByteArray &key,
                  QByteArray *plaintext)
{
    if (plaintext == nullptr || nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
        || ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return false;
    }

    QByteArray decrypted(ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES,
                         Qt::Uninitialized);
    unsigned long long decryptedLength = 0;
    const int result =
        crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(decrypted.data()),
            &decryptedLength,
            nullptr,
            reinterpret_cast<const unsigned char *>(ciphertext.constData()),
            static_cast<unsigned long long>(ciphertext.size()),
            additionalData.isEmpty()
                ? nullptr
                : reinterpret_cast<const unsigned char *>(additionalData.constData()),
            static_cast<unsigned long long>(additionalData.size()),
            reinterpret_cast<const unsigned char *>(nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    if (result != 0) {
        NoteCrypto::wipe(&decrypted);
        return false;
    }

    decrypted.truncate(static_cast<int>(decryptedLength));
    *plaintext = decrypted;
    return true;
}

QByteArray serializePayload(const QString &title, const QString &content)
{
    QJsonObject root;
    root.insert(QStringLiteral("title"), title);
    root.insert(QStringLiteral("content"), content);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parsePayload(const QByteArray &payload,
                  NoteCrypto::UnlockedNoteData *unlocked,
                  QString *errorMessage)
{
    if (unlocked == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing decryption output"));
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        setError(errorMessage, tr_c("Note ciphertext is corrupted"));
        return false;
    }

    const QJsonObject root = document.object();
    unlocked->title = root.value(QStringLiteral("title")).toString();
    unlocked->content = root.value(QStringLiteral("content")).toString();
    return true;
}

bool wrapDataKey(qint64 noteId,
                 const QString &password,
                 const QByteArray &dataKey,
                 const char *purpose,
                 NoteCrypto::WrappedKeyData *wrap,
                 QString *errorMessage)
{
    if (wrap == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing key wrap output"));
        return false;
    }

    QByteArray salt(crypto_pwhash_SALTBYTES, Qt::Uninitialized);
    randombytes_buf(salt.data(), static_cast<size_t>(salt.size()));

    QByteArray derivedKey;
    if (!deriveKey(password,
                   salt,
                   kWrapOpsLimit,
                   kWrapMemLimit,
                   crypto_pwhash_ALG_ARGON2ID13,
                   &derivedKey,
                   errorMessage)) {
        return false;
    }

    const bool encrypted = encryptBytes(dataKey,
                                        associatedData(noteId, purpose),
                                        derivedKey,
                                        &wrap->ciphertext,
                                        &wrap->nonce,
                                        errorMessage);
    NoteCrypto::wipe(&derivedKey);
    if (!encrypted) {
        return false;
    }

    wrap->salt = salt;
    wrap->opsLimit = kWrapOpsLimit;
    wrap->memLimit = kWrapMemLimit;
    wrap->algorithm = crypto_pwhash_ALG_ARGON2ID13;
    return true;
}

bool unwrapDataKey(qint64 noteId,
                   const QString &password,
                   const NoteCrypto::WrappedKeyData &wrap,
                   const char *purpose,
                   QByteArray *dataKey)
{
    QByteArray derivedKey;
    if (!deriveKey(password,
                   wrap.salt,
                   wrap.opsLimit,
                   wrap.memLimit,
                   wrap.algorithm,
                   &derivedKey,
                   nullptr)) {
        return false;
    }

    const bool okay = decryptBytes(wrap.ciphertext,
                                   associatedData(noteId, purpose),
                                   wrap.nonce,
                                   derivedKey,
                                   dataKey);
    NoteCrypto::wipe(&derivedKey);
    if (!okay || dataKey == nullptr
        || dataKey->size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        if (dataKey != nullptr) {
            NoteCrypto::wipe(dataKey);
        }
        return false;
    }
    return true;
}

} // namespace

bool NoteCrypto::initialize(QString *errorMessage)
{
    if (sodium_init() < 0) {
        setError(errorMessage, tr_c("Failed to initialize crypto library"));
        return false;
    }
    return true;
}

bool NoteCrypto::looksAcceptableSimplePassword(const QString &password, QString *errorMessage)
{
    if (password.size() < 4) {
        setError(errorMessage, tr_c("Simple password must be at least 4 characters"));
        return false;
    }
    return true;
}

bool NoteCrypto::looksStrongRecoveryPassword(const QString &password, QString *errorMessage)
{
    if (password.size() < 12) {
        setError(errorMessage, tr_c("Recovery password must be at least 12 characters"));
        return false;
    }

    if (password.size() >= 16) {
        return true;
    }

    bool hasLower = false;
    bool hasUpper = false;
    bool hasDigit = false;
    bool hasOther = false;
    for (QChar ch : password) {
        if (ch.isLower()) {
            hasLower = true;
        } else if (ch.isUpper()) {
            hasUpper = true;
        } else if (ch.isDigit()) {
            hasDigit = true;
        } else {
            hasOther = true;
        }
    }

    const int categoryCount =
        int(hasLower) + int(hasUpper) + int(hasDigit) + int(hasOther);
    if (categoryCount < 3) {
        setError(errorMessage,
                 tr_c("Recovery password must contain at least 3 character categories: lowercase, uppercase, digits, symbols"));
        return false;
    }

    return true;
}

bool NoteCrypto::createPasswordVerifier(const QString &password,
                                        QByteArray *verifier,
                                        QString *errorMessage)
{
    if (verifier == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing password verifier output"));
        return false;
    }

    QByteArray passwordUtf8 = password.toUtf8();
    QByteArray hash(crypto_pwhash_STRBYTES, Qt::Uninitialized);
    const int result =
        crypto_pwhash_str_alg(hash.data(),
                              passwordUtf8.constData(),
                              static_cast<unsigned long long>(passwordUtf8.size()),
                              crypto_pwhash_OPSLIMIT_MODERATE,
                              crypto_pwhash_MEMLIMIT_MODERATE,
                              crypto_pwhash_ALG_ARGON2ID13);
    sodium_memzero(passwordUtf8.data(), static_cast<size_t>(passwordUtf8.size()));
    if (result != 0) {
        setError(errorMessage, tr_c("Failed to generate password verifier"));
        return false;
    }

    *verifier = QByteArray(hash.constData());
    NoteCrypto::wipe(&hash);
    return true;
}

bool NoteCrypto::verifyPasswordVerifier(const QString &password, const QByteArray &verifier)
{
    if (verifier.isEmpty()) {
        return false;
    }

    QByteArray passwordUtf8 = password.toUtf8();
    const int result = crypto_pwhash_str_verify(verifier.constData(),
                                                passwordUtf8.constData(),
                                                static_cast<unsigned long long>(passwordUtf8.size()));
    sodium_memzero(passwordUtf8.data(), static_cast<size_t>(passwordUtf8.size()));
    return result == 0;
}

bool NoteCrypto::encryptForNewNote(qint64 noteId,
                                   const QString &title,
                                   const QString &content,
                                   const QString &simplePassword,
                                   const QString &recoveryPassword,
                                   EncryptedNoteData *encrypted,
                                   UnlockedNoteData *unlocked,
                                   QString *errorMessage)
{
    if (encrypted == nullptr || unlocked == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing encryption output"));
        return false;
    }

    QString validationError;
    if (!looksAcceptableSimplePassword(simplePassword, &validationError)
        || !looksStrongRecoveryPassword(recoveryPassword, &validationError)) {
        setError(errorMessage, validationError);
        return false;
    }

    QByteArray dataKey(crypto_aead_xchacha20poly1305_ietf_KEYBYTES, Qt::Uninitialized);
    randombytes_buf(dataKey.data(), static_cast<size_t>(dataKey.size()));

    QByteArray payloadCiphertext;
    QByteArray payloadNonce;
    if (!encryptWithExistingKey(noteId,
                                title,
                                content,
                                dataKey,
                                &payloadCiphertext,
                                &payloadNonce,
                                errorMessage)) {
        wipe(&dataKey);
        return false;
    }

    EncryptedNoteData result;
    result.version = kCryptoVersion;
    result.payloadCiphertext = payloadCiphertext;
    result.payloadNonce = payloadNonce;

    if (!wrapDataKey(noteId,
                     simplePassword,
                     dataKey,
                     "simple-wrap",
                     &result.simpleWrap,
                     errorMessage)
        || !wrapDataKey(noteId,
                        recoveryPassword,
                        dataKey,
                        "recovery-wrap",
                        &result.recoveryWrap,
                        errorMessage)) {
        wipe(&dataKey);
        return false;
    }

    unlocked->title = title;
    unlocked->content = content;
    unlocked->dataKey = dataKey;
    *encrypted = result;
    return true;
}

bool NoteCrypto::encryptWithExistingKey(qint64 noteId,
                                        const QString &title,
                                        const QString &content,
                                        const QByteArray &dataKey,
                                        QByteArray *ciphertext,
                                        QByteArray *nonce,
                                        QString *errorMessage)
{
    if (dataKey.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        setError(errorMessage, tr_c("Current unlock state is invalid"));
        return false;
    }

    return encryptBytes(serializePayload(title, content),
                        associatedData(noteId, "payload"),
                        dataKey,
                        ciphertext,
                        nonce,
                        errorMessage);
}

bool NoteCrypto::rewrapDataKey(qint64 noteId,
                               const QString &oldPassword,
                               const QString &newPassword,
                               const WrappedKeyData &currentWrap,
                               bool useRecoveryPassword,
                               WrappedKeyData *nextWrap,
                               QString *errorMessage)
{
    if (nextWrap == nullptr) {
        setError(errorMessage, tr_c("Internal error: missing new key wrap output"));
        return false;
    }
    if (!validateWrapData(currentWrap)) {
        setError(errorMessage, tr_c("Key wrap data is corrupted"));
        return false;
    }

    QString validationError;
    const bool newPasswordValid =
        useRecoveryPassword ? looksStrongRecoveryPassword(newPassword, &validationError)
                            : looksAcceptableSimplePassword(newPassword, &validationError);
    if (!newPasswordValid) {
        setError(errorMessage, validationError);
        return false;
    }

    const char *purpose = useRecoveryPassword ? "recovery-wrap" : "simple-wrap";
    QByteArray dataKey;
    if (!unwrapDataKey(noteId, oldPassword, currentWrap, purpose, &dataKey)) {
        setError(errorMessage, tr_c("Current password is incorrect, cannot update encrypted note key"));
        return false;
    }

    const bool wrapped = wrapDataKey(noteId, newPassword, dataKey, purpose, nextWrap, errorMessage);
    wipe(&dataKey);
    return wrapped;
}

bool NoteCrypto::encryptAttachmentBytes(qint64 noteId,
                                        const QString &assetId,
                                        const QByteArray &plaintext,
                                        const QByteArray &dataKey,
                                        QByteArray *ciphertext,
                                        QByteArray *nonce,
                                        QString *errorMessage)
{
    if (assetId.trimmed().isEmpty()) {
        setError(errorMessage, tr_c("Invalid attachment identifier"));
        return false;
    }
    if (dataKey.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        setError(errorMessage, tr_c("Invalid attachment encryption key"));
        return false;
    }

    return encryptBytes(plaintext,
                        attachmentAssociatedData(noteId, assetId),
                        dataKey,
                        ciphertext,
                        nonce,
                        errorMessage);
}

bool NoteCrypto::decryptAttachmentBytes(qint64 noteId,
                                        const QString &assetId,
                                        const QByteArray &ciphertext,
                                        const QByteArray &nonce,
                                        const QByteArray &dataKey,
                                        QByteArray *plaintext,
                                        QString *errorMessage)
{
    if (assetId.trimmed().isEmpty()) {
        setError(errorMessage, tr_c("Invalid attachment identifier"));
        return false;
    }
    if (dataKey.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        setError(errorMessage, tr_c("Invalid attachment decryption key"));
        return false;
    }

    if (!decryptBytes(ciphertext,
                      attachmentAssociatedData(noteId, assetId),
                      nonce,
                      dataKey,
                      plaintext)) {
        setError(errorMessage, tr_c("Failed to decrypt attachment"));
        return false;
    }
    return true;
}

NoteCrypto::UnlockResult NoteCrypto::unlockWithPassword(qint64 noteId,
                                                        const EncryptedNoteData &encrypted,
                                                        const QString &password,
                                                        bool useRecoveryPassword)
{
    UnlockResult result;
    if (!validateEncryptedData(encrypted)) {
        result.status = UnlockStatus::InvalidData;
        result.errorMessage = tr_c("Note ciphertext is corrupted or incomplete");
        return result;
    }

    const WrappedKeyData &wrap = useRecoveryPassword ? encrypted.recoveryWrap : encrypted.simpleWrap;
    const char *purpose = useRecoveryPassword ? "recovery-wrap" : "simple-wrap";

    QByteArray dataKey;
    if (!unwrapDataKey(noteId, password, wrap, purpose, &dataKey)) {
        result.status = UnlockStatus::WrongPassword;
        return result;
    }

    QByteArray payload;
    if (!decryptBytes(encrypted.payloadCiphertext,
                      associatedData(noteId, "payload"),
                      encrypted.payloadNonce,
                      dataKey,
                      &payload)) {
        wipe(&dataKey);
        result.status = UnlockStatus::InvalidData;
        result.errorMessage = tr_c("Failed to decrypt note ciphertext");
        return result;
    }

    result.note.dataKey = dataKey;
    if (!parsePayload(payload, &result.note, &result.errorMessage)) {
        wipe(&result.note.dataKey);
        result.note = {};
        result.status = UnlockStatus::InvalidData;
        return result;
    }

    result.status = UnlockStatus::Success;
    return result;
}

void NoteCrypto::wipe(QByteArray *data)
{
    if (data == nullptr || data->isEmpty()) {
        return;
    }

    sodium_memzero(data->data(), static_cast<size_t>(data->size()));
    data->clear();
}
