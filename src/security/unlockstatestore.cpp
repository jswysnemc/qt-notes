#include "security/unlockstatestore.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef signals
#undef signals
#endif
#include <libsecret/secret.h>

namespace {

constexpr auto kUnlockStateSchemaName = "com.snemc.qt-notes.unlock-state";
constexpr auto kUnlockStateAppAttr = "application";
constexpr auto kUnlockStateNoteIdAttr = "note-id";
constexpr auto kUnlockStateAppValue = "qt-notes";

const SecretSchema kUnlockStateSchema = {
    kUnlockStateSchemaName,
    SECRET_SCHEMA_NONE,
    {
        {kUnlockStateAppAttr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        {kUnlockStateNoteIdAttr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
    },
};

QString noteIdAttributeValue(qint64 noteId)
{
    return QString::number(noteId);
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString fromGError(GError *error, const QString &fallback)
{
    if (error == nullptr || error->message == nullptr) {
        return fallback;
    }
    return QString::fromUtf8(error->message);
}

QString serializeState(const FailedUnlockState &state)
{
    QJsonObject root;
    root.insert(QStringLiteral("failed_attempts"), state.failedAttempts);
    root.insert(QStringLiteral("recovery_required"), state.recoveryRequired);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool parseState(const gchar *payload, FailedUnlockState *state, QString *errorMessage)
{
    if (state == nullptr) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Internal error: missing unlock state output"));
        return false;
    }
    if (payload == nullptr || *payload == '\0') {
        *state = {};
        return true;
    }

    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(payload));
    if (!document.isObject()) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Unlock state in system keyring is corrupted"));
        return false;
    }

    const QJsonObject root = document.object();
    const int failedAttempts = root.value(QStringLiteral("failed_attempts")).toInt(-1);
    if (failedAttempts < 0) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Invalid unlock attempt count in system keyring"));
        return false;
    }

    state->failedAttempts = failedAttempts;
    state->recoveryRequired = root.value(QStringLiteral("recovery_required")).toBool(false);
    return true;
}

} // namespace

bool UnlockStateStore::load(qint64 noteId,
                            FailedUnlockState *state,
                            QString *errorMessage) const
{
    if (noteId < 0) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Invalid note identifier"));
        return false;
    }
    if (state == nullptr) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Internal error: missing unlock state output"));
        return false;
    }

    GError *error = nullptr;
    const QByteArray noteIdUtf8 = noteIdAttributeValue(noteId).toUtf8();
    gchar *secret = secret_password_lookup_sync(&kUnlockStateSchema,
                                                nullptr,
                                                &error,
                                                kUnlockStateAppAttr,
                                                kUnlockStateAppValue,
                                                kUnlockStateNoteIdAttr,
                                                noteIdUtf8.constData(),
                                                nullptr);
    if (error != nullptr) {
        const QString message = fromGError(error, QCoreApplication::translate("UnlockStateStore", "Failed to read unlock state from system keyring"));
        g_error_free(error);
        setError(errorMessage, message);
        return false;
    }

    const bool ok = parseState(secret, state, errorMessage);
    secret_password_free(secret);
    return ok;
}

bool UnlockStateStore::save(qint64 noteId,
                            const FailedUnlockState &state,
                            QString *errorMessage) const
{
    if (noteId < 0) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Invalid note identifier"));
        return false;
    }
    if (state.failedAttempts <= 0 && !state.recoveryRequired) {
        return clear(noteId, errorMessage);
    }

    GError *error = nullptr;
    const QByteArray noteIdUtf8 = noteIdAttributeValue(noteId).toUtf8();
    const QByteArray secret = serializeState(state).toUtf8();
    const QByteArray label =
        QStringLiteral("qt-notes 解锁状态 %1").arg(noteId).toUtf8();

    const gboolean stored = secret_password_store_sync(&kUnlockStateSchema,
                                                       SECRET_COLLECTION_DEFAULT,
                                                       label.constData(),
                                                       secret.constData(),
                                                       nullptr,
                                                       &error,
                                                       kUnlockStateAppAttr,
                                                       kUnlockStateAppValue,
                                                       kUnlockStateNoteIdAttr,
                                                       noteIdUtf8.constData(),
                                                       nullptr);
    if (!stored) {
        const QString message = fromGError(error, QCoreApplication::translate("UnlockStateStore", "Failed to write unlock state to system keyring"));
        if (error != nullptr) {
            g_error_free(error);
        }
        setError(errorMessage, message);
        return false;
    }

    return true;
}

bool UnlockStateStore::clear(qint64 noteId, QString *errorMessage) const
{
    if (noteId < 0) {
        setError(errorMessage, QCoreApplication::translate("UnlockStateStore", "Invalid note identifier"));
        return false;
    }

    GError *error = nullptr;
    const QByteArray noteIdUtf8 = noteIdAttributeValue(noteId).toUtf8();
    const gboolean cleared = secret_password_clear_sync(&kUnlockStateSchema,
                                                        nullptr,
                                                        &error,
                                                        kUnlockStateAppAttr,
                                                        kUnlockStateAppValue,
                                                        kUnlockStateNoteIdAttr,
                                                        noteIdUtf8.constData(),
                                                        nullptr);
    if (!cleared && error != nullptr) {
        const QString message = fromGError(error, QCoreApplication::translate("UnlockStateStore", "Failed to clear unlock state from system keyring"));
        g_error_free(error);
        setError(errorMessage, message);
        return false;
    }

    if (error != nullptr) {
        g_error_free(error);
    }
    return true;
}
