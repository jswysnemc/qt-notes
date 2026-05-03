#include "data/notesrepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

constexpr auto kConnectionName = "qt-notes-main-connection";
constexpr auto kDefaultThemeId = "paper";
constexpr auto kDefaultFontSize = 14;

struct StoredEncryptedRecord {
    NoteData note;
    int encryptionVersion = 0;
    QByteArray payloadCiphertext;
    QByteArray payloadNonce;
    NoteCrypto::WrappedKeyData simpleWrap;
    NoteCrypto::WrappedKeyData recoveryWrap;
};

NoteData noteFromQuery(const QSqlQuery &query)
{
    NoteData note;
    note.id = query.value(QStringLiteral("id")).toLongLong();
    note.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    note.updatedAt = query.value(QStringLiteral("updated_at")).toLongLong();
    note.themeId = query.value(QStringLiteral("theme_id")).toString();
    note.wrapMode = query.value(QStringLiteral("wrap_mode")).toInt() != 0;
    note.fontFamily = query.value(QStringLiteral("font_family")).toString();
    note.fontPointSize = query.value(QStringLiteral("font_point_size")).toInt();
    note.windowGeometry = query.value(QStringLiteral("window_geometry")).toByteArray();
    note.isEncrypted = query.value(QStringLiteral("encrypted")).toInt() != 0;
    note.failedUnlockAttempts = 0;
    note.recoveryPasswordRequired = false;

    if (note.isEncrypted) {
        note.title = query.value(QStringLiteral("title")).toString().trimmed();
        if (note.title.isEmpty()) {
            note.title = maskedEncryptedTitle(QString());
        }
        note.content.clear();
    } else {
        note.title = query.value(QStringLiteral("title")).toString();
        note.content = query.value(QStringLiteral("content")).toString();
    }
    return note;
}

NoteSummary summaryFromQuery(const QSqlQuery &query)
{
    NoteSummary summary;
    summary.id = query.value(QStringLiteral("id")).toLongLong();
    summary.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    summary.updatedAt = query.value(QStringLiteral("updated_at")).toLongLong();
    summary.themeId = query.value(QStringLiteral("theme_id")).toString();
    summary.isEncrypted = query.value(QStringLiteral("encrypted")).toInt() != 0;
    summary.recoveryPasswordRequired = false;
    summary.title = query.value(QStringLiteral("title")).toString();
    if (summary.isEncrypted) {
        summary.title = summary.title.trimmed();
        if (summary.title.isEmpty()) {
            summary.title = maskedEncryptedTitle(QString());
        }
    }
    return summary;
}

bool execStatement(QSqlDatabase &database, const QString &sql, QString *errorMessage)
{
    QSqlQuery query(database);
    if (query.exec(sql)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

bool columnExists(QSqlDatabase &database, const QString &tableName, const QString &columnName)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return false;
    }

    while (query.next()) {
        if (query.value(QStringLiteral("name")).toString() == columnName) {
            return true;
        }
    }
    return false;
}

bool ensureColumn(QSqlDatabase &database,
                  const QString &tableName,
                  const QString &columnName,
                  const QString &definition,
                  QString *errorMessage)
{
    if (columnExists(database, tableName, columnName)) {
        return true;
    }

    return execStatement(database,
                         QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, definition),
                         errorMessage);
}

bool loadStoredEncryptedRecord(QSqlDatabase &database,
                               qint64 id,
                               StoredEncryptedRecord *record,
                               QString *errorMessage = nullptr)
{
    if (record == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Internal error: missing encrypted note output");
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, title, content, created_at, updated_at, theme_id, wrap_mode, font_family, "
        "font_point_size, window_geometry, encrypted, failed_unlock_attempts, "
        "recovery_password_required, encryption_version, encrypted_payload, "
        "encrypted_payload_nonce, simple_wrap_ciphertext, simple_wrap_nonce, simple_wrap_salt, "
        "simple_wrap_opslimit, simple_wrap_memlimit, simple_wrap_algorithm, "
        "recovery_wrap_ciphertext, recovery_wrap_nonce, recovery_wrap_salt, "
        "recovery_wrap_opslimit, recovery_wrap_memlimit, recovery_wrap_algorithm "
        "FROM notes WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().isValid() ? query.lastError().text()
                                                        : QCoreApplication::translate("NotesRepository", "Note not found");
        }
        return false;
    }

    record->note = noteFromQuery(query);
    record->encryptionVersion = query.value(QStringLiteral("encryption_version")).toInt();
    record->payloadCiphertext = query.value(QStringLiteral("encrypted_payload")).toByteArray();
    record->payloadNonce = query.value(QStringLiteral("encrypted_payload_nonce")).toByteArray();
    record->simpleWrap.ciphertext =
        query.value(QStringLiteral("simple_wrap_ciphertext")).toByteArray();
    record->simpleWrap.nonce = query.value(QStringLiteral("simple_wrap_nonce")).toByteArray();
    record->simpleWrap.salt = query.value(QStringLiteral("simple_wrap_salt")).toByteArray();
    record->simpleWrap.opsLimit =
        query.value(QStringLiteral("simple_wrap_opslimit")).toULongLong();
    record->simpleWrap.memLimit =
        query.value(QStringLiteral("simple_wrap_memlimit")).toULongLong();
    record->simpleWrap.algorithm =
        query.value(QStringLiteral("simple_wrap_algorithm")).toInt();
    record->recoveryWrap.ciphertext =
        query.value(QStringLiteral("recovery_wrap_ciphertext")).toByteArray();
    record->recoveryWrap.nonce = query.value(QStringLiteral("recovery_wrap_nonce")).toByteArray();
    record->recoveryWrap.salt = query.value(QStringLiteral("recovery_wrap_salt")).toByteArray();
    record->recoveryWrap.opsLimit =
        query.value(QStringLiteral("recovery_wrap_opslimit")).toULongLong();
    record->recoveryWrap.memLimit =
        query.value(QStringLiteral("recovery_wrap_memlimit")).toULongLong();
    record->recoveryWrap.algorithm =
        query.value(QStringLiteral("recovery_wrap_algorithm")).toInt();
    return true;
}

} // namespace

NotesRepository::NotesRepository() = default;

NotesRepository::~NotesRepository()
{
    if (database_.isValid()) {
        const QString connectionName = database_.connectionName();
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool NotesRepository::initialize(QString *errorMessage)
{
    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDirectory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Failed to determine application data directory");
        }
        qWarning() << "NotesRepository initialize failed: empty app data path";
        return false;
    }

    QDir directory;
    if (!directory.mkpath(dataDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Failed to create data directory: %1").arg(dataDirectory);
        }
        qWarning() << "NotesRepository initialize failed: cannot create data directory"
                   << dataDirectory;
        return false;
    }

    databasePath_ = QDir(dataDirectory).filePath(QStringLiteral("notes.db"));

    if (QSqlDatabase::contains(QLatin1StringView(kConnectionName))) {
        database_ = QSqlDatabase::database(QLatin1StringView(kConnectionName));
    } else {
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                              QLatin1StringView(kConnectionName));
    }

    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        qWarning() << "NotesRepository initialize failed: open database error"
                   << database_.lastError().text();
        return false;
    }

    if (!execStatement(database_, QStringLiteral("PRAGMA secure_delete = ON"), errorMessage)) {
        return false;
    }

    return createSchema(errorMessage);
}

NoteData NotesRepository::createNote()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO notes "
        "(title, content, created_at, updated_at, theme_id, wrap_mode, font_family, "
        "font_point_size, window_geometry, encrypted, failed_unlock_attempts, "
        "recovery_password_required, encryption_version) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0, 0)"));
    query.addBindValue(timestampTitle(now));
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(QString::fromLatin1(kDefaultThemeId));
    query.addBindValue(1);
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(kDefaultFontSize);
    query.addBindValue(QByteArray());
    if (!query.exec()) {
        qWarning() << "NotesRepository createNote failed:" << query.lastError().text();
        return {};
    }

    return noteById(query.lastInsertId().toLongLong()).value_or(NoteData{});
}

std::optional<NoteData> NotesRepository::noteById(qint64 id)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, title, content, created_at, updated_at, theme_id, wrap_mode, font_family, "
        "font_point_size, window_geometry, encrypted, failed_unlock_attempts, "
        "recovery_password_required "
        "FROM notes WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    NoteData note = noteFromQuery(query);
    applyUnlockStateSoft(&note);
    return note;
}

QVector<NoteSummary> NotesRepository::noteSummaries(SortMode sortMode)
{
    QVector<NoteSummary> notes;
    QSqlQuery query(database_);
    if (!query.exec(
            QStringLiteral("SELECT id, title, created_at, updated_at, theme_id, encrypted, "
                           "recovery_password_required FROM notes ORDER BY %1")
                .arg(orderClause(sortMode)))) {
        return notes;
    }

    while (query.next()) {
        NoteSummary summary = summaryFromQuery(query);
        applyUnlockStateSoft(&summary);
        notes.push_back(summary);
    }
    return notes;
}

qint64 NotesRepository::startupNoteId()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id FROM notes ORDER BY updated_at DESC, id DESC LIMIT 1"))
        || !query.next()) {
        return -1;
    }

    return query.value(0).toLongLong();
}

qint64 NotesRepository::latestCreatedNoteId()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id FROM notes ORDER BY created_at DESC, id DESC LIMIT 1"))
        || !query.next()) {
        return -1;
    }

    return query.value(0).toLongLong();
}

NoteEncryptionResult NotesRepository::enableEncryption(qint64 id,
                                                       const QString &title,
                                                       const QString &content,
                                                       const QString &simplePassword,
                                                       const QString &recoveryPassword)
{
    NoteEncryptionResult result;
    const std::optional<NoteData> existing = noteById(id);
    if (!existing.has_value()) {
        result.errorMessage = QCoreApplication::translate("NotesRepository", "Note not found");
        return result;
    }
    if (existing->isEncrypted) {
        result.errorMessage = QCoreApplication::translate("NotesRepository", "Note is already encrypted");
        return result;
    }

    NoteCrypto::EncryptedNoteData encrypted;
    NoteCrypto::UnlockedNoteData unlocked;
    if (!NoteCrypto::encryptForNewNote(id,
                                       title,
                                       content,
                                       simplePassword,
                                       recoveryPassword,
                                       &encrypted,
                                       &unlocked,
                                       &result.errorMessage)) {
        return result;
    }

    if (!unlockStateStore_.clear(id, &result.errorMessage)) {
        NoteCrypto::wipe(&unlocked.dataKey);
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!database_.transaction()) {
        result.errorMessage = database_.lastError().text();
        NoteCrypto::wipe(&unlocked.dataKey);
        return result;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes SET "
        "title = ?, content = '', updated_at = ?, encrypted = 1, encryption_version = ?, "
        "failed_unlock_attempts = 0, recovery_password_required = 0, "
        "encrypted_payload = ?, encrypted_payload_nonce = ?, "
        "simple_wrap_ciphertext = ?, simple_wrap_nonce = ?, simple_wrap_salt = ?, "
        "simple_wrap_opslimit = ?, simple_wrap_memlimit = ?, simple_wrap_algorithm = ?, "
        "recovery_wrap_ciphertext = ?, recovery_wrap_nonce = ?, recovery_wrap_salt = ?, "
        "recovery_wrap_opslimit = ?, recovery_wrap_memlimit = ?, recovery_wrap_algorithm = ? "
        "WHERE id = ?"));
    query.addBindValue(maskedEncryptedTitle(title));
    query.addBindValue(now);
    query.addBindValue(encrypted.version);
    query.addBindValue(encrypted.payloadCiphertext);
    query.addBindValue(encrypted.payloadNonce);
    query.addBindValue(encrypted.simpleWrap.ciphertext);
    query.addBindValue(encrypted.simpleWrap.nonce);
    query.addBindValue(encrypted.simpleWrap.salt);
    query.addBindValue(static_cast<qulonglong>(encrypted.simpleWrap.opsLimit));
    query.addBindValue(static_cast<qulonglong>(encrypted.simpleWrap.memLimit));
    query.addBindValue(encrypted.simpleWrap.algorithm);
    query.addBindValue(encrypted.recoveryWrap.ciphertext);
    query.addBindValue(encrypted.recoveryWrap.nonce);
    query.addBindValue(encrypted.recoveryWrap.salt);
    query.addBindValue(static_cast<qulonglong>(encrypted.recoveryWrap.opsLimit));
    query.addBindValue(static_cast<qulonglong>(encrypted.recoveryWrap.memLimit));
    query.addBindValue(encrypted.recoveryWrap.algorithm);
    query.addBindValue(id);
    if (!query.exec() || !database_.commit()) {
        database_.rollback();
        result.errorMessage = query.lastError().isValid() ? query.lastError().text()
                                                          : database_.lastError().text();
        NoteCrypto::wipe(&unlocked.dataKey);
        return result;
    }

    if (!execStatement(database_, QStringLiteral("VACUUM"), nullptr)) {
        qWarning() << "NotesRepository enableEncryption vacuum failed";
    }

    result.success = true;
    result.dataKey = unlocked.dataKey;
    result.note = existing.value();
    result.note.title = unlocked.title;
    result.note.content = unlocked.content;
    result.note.updatedAt = now;
    result.note.isEncrypted = true;
    result.note.failedUnlockAttempts = 0;
    result.note.recoveryPasswordRequired = false;
    return result;
}

NoteUnlockAttemptResult NotesRepository::unlockWithSimplePassword(qint64 id, const QString &password)
{
    NoteUnlockAttemptResult result;
    StoredEncryptedRecord record;
    if (!loadStoredEncryptedRecord(database_, id, &record, &result.errorMessage)) {
        return result;
    }
    FailedUnlockState state;
    if (!loadUnlockStateStrict(id, record.note.isEncrypted, &state, &result.errorMessage)) {
        result.status = NoteCrypto::UnlockStatus::InvalidData;
        return result;
    }
    record.note.failedUnlockAttempts = state.failedAttempts;
    record.note.recoveryPasswordRequired = state.recoveryRequired;

    if (!record.note.isEncrypted) {
        result.status = NoteCrypto::UnlockStatus::NotEncrypted;
        result.note = record.note;
        return result;
    }

    if (record.note.recoveryPasswordRequired) {
        result.status = NoteCrypto::UnlockStatus::RecoveryPasswordRequired;
        result.note = record.note;
        result.failedUnlockAttempts = record.note.failedUnlockAttempts;
        return result;
    }

    NoteCrypto::EncryptedNoteData encrypted;
    encrypted.version = record.encryptionVersion;
    encrypted.payloadCiphertext = record.payloadCiphertext;
    encrypted.payloadNonce = record.payloadNonce;
    encrypted.simpleWrap = record.simpleWrap;
    encrypted.recoveryWrap = record.recoveryWrap;

    NoteCrypto::UnlockResult unlockResult =
        NoteCrypto::unlockWithPassword(id, encrypted, password, false);
    result.status = unlockResult.status;
    result.errorMessage = unlockResult.errorMessage;
    result.note = record.note;

    if (unlockResult.status == NoteCrypto::UnlockStatus::Success) {
        unlockStateStore_.clear(id, nullptr);
        result.note.title = unlockResult.note.title;
        result.note.content = unlockResult.note.content;
        result.note.failedUnlockAttempts = 0;
        result.note.recoveryPasswordRequired = false;
        result.dataKey = unlockResult.note.dataKey;
        return result;
    }

    if (unlockResult.status != NoteCrypto::UnlockStatus::WrongPassword) {
        return result;
    }

    const int failedAttempts =
        qMin(record.note.failedUnlockAttempts + 1, NoteCrypto::kFailedAttemptsBeforeRecovery);
    const bool recoveryRequired = failedAttempts >= NoteCrypto::kFailedAttemptsBeforeRecovery;
    FailedUnlockState nextState;
    nextState.failedAttempts = failedAttempts;
    nextState.recoveryRequired = recoveryRequired;
    if (!unlockStateStore_.save(id, nextState, &result.errorMessage)) {
        result.status = NoteCrypto::UnlockStatus::InvalidData;
        return result;
    }
    result.failedUnlockAttempts = failedAttempts;
    result.remainingSimpleAttempts =
        qMax(0, NoteCrypto::kFailedAttemptsBeforeRecovery - failedAttempts);
    result.note.failedUnlockAttempts = failedAttempts;
    result.note.recoveryPasswordRequired = recoveryRequired;
    if (recoveryRequired) {
        result.status = NoteCrypto::UnlockStatus::RecoveryPasswordRequired;
    }
    return result;
}

NoteUnlockAttemptResult NotesRepository::unlockWithRecoveryPassword(qint64 id,
                                                                    const QString &password)
{
    NoteUnlockAttemptResult result;
    StoredEncryptedRecord record;
    if (!loadStoredEncryptedRecord(database_, id, &record, &result.errorMessage)) {
        return result;
    }
    FailedUnlockState state;
    if (!loadUnlockStateStrict(id, record.note.isEncrypted, &state, &result.errorMessage)) {
        result.status = NoteCrypto::UnlockStatus::InvalidData;
        return result;
    }
    record.note.failedUnlockAttempts = state.failedAttempts;
    record.note.recoveryPasswordRequired = state.recoveryRequired;

    if (!record.note.isEncrypted) {
        result.status = NoteCrypto::UnlockStatus::NotEncrypted;
        result.note = record.note;
        return result;
    }

    NoteCrypto::EncryptedNoteData encrypted;
    encrypted.version = record.encryptionVersion;
    encrypted.payloadCiphertext = record.payloadCiphertext;
    encrypted.payloadNonce = record.payloadNonce;
    encrypted.simpleWrap = record.simpleWrap;
    encrypted.recoveryWrap = record.recoveryWrap;

    NoteCrypto::UnlockResult unlockResult =
        NoteCrypto::unlockWithPassword(id, encrypted, password, true);
    result.status = unlockResult.status;
    result.errorMessage = unlockResult.errorMessage;
    result.note = record.note;
    result.failedUnlockAttempts = record.note.failedUnlockAttempts;

    if (unlockResult.status == NoteCrypto::UnlockStatus::Success) {
        unlockStateStore_.clear(id, nullptr);
        result.note.title = unlockResult.note.title;
        result.note.content = unlockResult.note.content;
        result.note.failedUnlockAttempts = 0;
        result.note.recoveryPasswordRequired = false;
        result.dataKey = unlockResult.note.dataKey;
    }
    return result;
}

bool NotesRepository::rewrapSimplePassword(const QString &oldPassword,
                                           const QString &newPassword,
                                           QString *errorMessage)
{
    QVector<qint64> encryptedNoteIds;
    QSqlQuery idQuery(database_);
    if (!idQuery.exec(QStringLiteral("SELECT id FROM notes WHERE encrypted = 1 ORDER BY id"))) {
        if (errorMessage != nullptr) {
            *errorMessage = idQuery.lastError().text();
        }
        return false;
    }
    while (idQuery.next()) {
        encryptedNoteIds.push_back(idQuery.value(0).toLongLong());
    }
    idQuery.finish();

    if (!database_.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        return false;
    }

    for (qint64 id : encryptedNoteIds) {
        StoredEncryptedRecord record;
        if (!loadStoredEncryptedRecord(database_, id, &record, errorMessage)) {
            database_.rollback();
            return false;
        }

        NoteCrypto::WrappedKeyData nextWrap;
        if (!NoteCrypto::rewrapDataKey(id,
                                       oldPassword,
                                       newPassword,
                                       record.simpleWrap,
                                       false,
                                       &nextWrap,
                                       errorMessage)) {
            database_.rollback();
            return false;
        }

        QSqlQuery updateQuery(database_);
        updateQuery.prepare(QStringLiteral(
            "UPDATE notes SET simple_wrap_ciphertext = ?, simple_wrap_nonce = ?, "
            "simple_wrap_salt = ?, simple_wrap_opslimit = ?, simple_wrap_memlimit = ?, "
            "simple_wrap_algorithm = ? WHERE id = ? AND encrypted = 1"));
        updateQuery.addBindValue(nextWrap.ciphertext);
        updateQuery.addBindValue(nextWrap.nonce);
        updateQuery.addBindValue(nextWrap.salt);
        updateQuery.addBindValue(static_cast<qulonglong>(nextWrap.opsLimit));
        updateQuery.addBindValue(static_cast<qulonglong>(nextWrap.memLimit));
        updateQuery.addBindValue(nextWrap.algorithm);
        updateQuery.addBindValue(id);
        if (!updateQuery.exec() || updateQuery.numRowsAffected() <= 0) {
            database_.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = updateQuery.lastError().isValid()
                                    ? updateQuery.lastError().text()
                                    : QCoreApplication::translate("NotesRepository", "Failed to update simple password for encrypted note");
            }
            return false;
        }
    }

    if (!database_.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        return false;
    }
    return true;
}

bool NotesRepository::rewrapRecoveryPassword(const QString &oldPassword,
                                             const QString &newPassword,
                                             QString *errorMessage)
{
    QVector<qint64> encryptedNoteIds;
    QSqlQuery idQuery(database_);
    if (!idQuery.exec(QStringLiteral("SELECT id FROM notes WHERE encrypted = 1 ORDER BY id"))) {
        if (errorMessage != nullptr) {
            *errorMessage = idQuery.lastError().text();
        }
        return false;
    }
    while (idQuery.next()) {
        encryptedNoteIds.push_back(idQuery.value(0).toLongLong());
    }
    idQuery.finish();

    if (!database_.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        return false;
    }

    for (qint64 id : encryptedNoteIds) {
        StoredEncryptedRecord record;
        if (!loadStoredEncryptedRecord(database_, id, &record, errorMessage)) {
            database_.rollback();
            return false;
        }

        NoteCrypto::WrappedKeyData nextWrap;
        if (!NoteCrypto::rewrapDataKey(id,
                                       oldPassword,
                                       newPassword,
                                       record.recoveryWrap,
                                       true,
                                       &nextWrap,
                                       errorMessage)) {
            database_.rollback();
            return false;
        }

        QSqlQuery updateQuery(database_);
        updateQuery.prepare(QStringLiteral(
            "UPDATE notes SET recovery_wrap_ciphertext = ?, recovery_wrap_nonce = ?, "
            "recovery_wrap_salt = ?, recovery_wrap_opslimit = ?, recovery_wrap_memlimit = ?, "
            "recovery_wrap_algorithm = ? WHERE id = ? AND encrypted = 1"));
        updateQuery.addBindValue(nextWrap.ciphertext);
        updateQuery.addBindValue(nextWrap.nonce);
        updateQuery.addBindValue(nextWrap.salt);
        updateQuery.addBindValue(static_cast<qulonglong>(nextWrap.opsLimit));
        updateQuery.addBindValue(static_cast<qulonglong>(nextWrap.memLimit));
        updateQuery.addBindValue(nextWrap.algorithm);
        updateQuery.addBindValue(id);
        if (!updateQuery.exec() || updateQuery.numRowsAffected() <= 0) {
            database_.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = updateQuery.lastError().isValid()
                                    ? updateQuery.lastError().text()
                                    : QCoreApplication::translate("NotesRepository", "Failed to update recovery password for encrypted note");
            }
            return false;
        }
    }

    if (!database_.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        return false;
    }
    return true;
}

bool NotesRepository::updateTitle(qint64 id, const QString &title)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("UPDATE notes SET title = ?, updated_at = ? WHERE id = ? AND encrypted = 0"));
    query.addBindValue(title);
    query.addBindValue(now);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool NotesRepository::updateContent(qint64 id, const QString &content)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes SET content = ?, updated_at = ? WHERE id = ? AND encrypted = 0"));
    query.addBindValue(content);
    query.addBindValue(now);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool NotesRepository::updateEncryptedNote(qint64 id,
                                          const QString &title,
                                          const QString &content,
                                          const QByteArray &dataKey,
                                          QString *errorMessage)
{
    QByteArray ciphertext;
    QByteArray nonce;
    if (!NoteCrypto::encryptWithExistingKey(id,
                                            title,
                                            content,
                                            dataKey,
                                            &ciphertext,
                                            &nonce,
                                            errorMessage)) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes SET title = ?, content = '', updated_at = ?, encrypted = 1, "
        "encrypted_payload = ?, encrypted_payload_nonce = ? "
        "WHERE id = ? AND encrypted = 1"));
    query.addBindValue(maskedEncryptedTitle(title));
    query.addBindValue(now);
    query.addBindValue(ciphertext);
    query.addBindValue(nonce);
    query.addBindValue(id);
    if (query.exec() && query.numRowsAffected() > 0) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

bool NotesRepository::disableEncryption(qint64 id,
                                        const QString &title,
                                        const QString &content,
                                        QString *errorMessage)
{
    const std::optional<NoteData> existing = noteById(id);
    if (!existing.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Note not found");
        }
        return false;
    }
    if (!existing->isEncrypted) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Note is not encrypted");
        }
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!database_.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database_.lastError().text();
        }
        return false;
    }

    const QByteArray emptyBlob;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes SET title = ?, content = ?, updated_at = ?, encrypted = 0, "
        "failed_unlock_attempts = 0, recovery_password_required = 0, encryption_version = 0, "
        "encrypted_payload = ?, encrypted_payload_nonce = ?, "
        "simple_wrap_ciphertext = ?, simple_wrap_nonce = ?, simple_wrap_salt = ?, "
        "simple_wrap_opslimit = 0, simple_wrap_memlimit = 0, simple_wrap_algorithm = 0, "
        "recovery_wrap_ciphertext = ?, recovery_wrap_nonce = ?, recovery_wrap_salt = ?, "
        "recovery_wrap_opslimit = 0, recovery_wrap_memlimit = 0, recovery_wrap_algorithm = 0 "
        "WHERE id = ? AND encrypted = 1"));
    query.addBindValue(title);
    query.addBindValue(content);
    query.addBindValue(now);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(emptyBlob);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() <= 0 || !database_.commit()) {
        database_.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().isValid() ? query.lastError().text()
                                                        : database_.lastError().text();
        }
        return false;
    }

    QString stateError;
    if (!unlockStateStore_.clear(id, &stateError)) {
        if (errorMessage != nullptr) {
            *errorMessage = stateError;
        }
        return false;
    }

    return true;
}

bool NotesRepository::updateAppearance(qint64 id, const QString &themeId, bool wrapMode)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes SET theme_id = ?, wrap_mode = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(themeId);
    query.addBindValue(wrapMode ? 1 : 0);
    query.addBindValue(now);
    query.addBindValue(id);
    return query.exec();
}

bool NotesRepository::updateGeometry(qint64 id, const QByteArray &geometry)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE notes SET window_geometry = ? WHERE id = ?"));
    query.addBindValue(geometry);
    query.addBindValue(id);
    return query.exec();
}

bool NotesRepository::deleteNote(qint64 id)
{
    unlockStateStore_.clear(id, nullptr);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM notes WHERE id = ?"));
    query.addBindValue(id);
    return query.exec();
}

QString NotesRepository::databasePath() const
{
    return databasePath_;
}

bool NotesRepository::loadUnlockStateStrict(qint64 noteId,
                                            bool encrypted,
                                            FailedUnlockState *state,
                                            QString *errorMessage) const
{
    if (state == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QCoreApplication::translate("NotesRepository", "Internal error: missing unlock state output");
        }
        return false;
    }

    if (!encrypted) {
        *state = {};
        return true;
    }

    if (!unlockStateStore_.load(noteId, state, errorMessage)) {
        return false;
    }

    state->failedAttempts =
        qBound(0, state->failedAttempts, NoteCrypto::kFailedAttemptsBeforeRecovery);
    if (state->failedAttempts >= NoteCrypto::kFailedAttemptsBeforeRecovery) {
        state->recoveryRequired = true;
    }
    return true;
}

void NotesRepository::applyUnlockStateSoft(NoteData *note) const
{
    if (note == nullptr) {
        return;
    }

    note->failedUnlockAttempts = 0;
    note->recoveryPasswordRequired = false;
    if (!note->isEncrypted) {
        return;
    }

    FailedUnlockState state;
    QString errorMessage;
    if (!loadUnlockStateStrict(note->id, true, &state, &errorMessage)) {
        note->failedUnlockAttempts = NoteCrypto::kFailedAttemptsBeforeRecovery;
        note->recoveryPasswordRequired = true;
        qWarning().noquote() << QStringLiteral(
            "NotesRepository applyUnlockStateSoft failed for note %1: %2")
                                      .arg(note->id)
                                      .arg(errorMessage);
        return;
    }

    note->failedUnlockAttempts = state.failedAttempts;
    note->recoveryPasswordRequired = state.recoveryRequired;
}

void NotesRepository::applyUnlockStateSoft(NoteSummary *summary) const
{
    if (summary == nullptr) {
        return;
    }

    summary->recoveryPasswordRequired = false;
    if (!summary->isEncrypted) {
        return;
    }

    FailedUnlockState state;
    QString errorMessage;
    if (!loadUnlockStateStrict(summary->id, true, &state, &errorMessage)) {
        summary->recoveryPasswordRequired = true;
        qWarning().noquote() << QStringLiteral(
            "NotesRepository applyUnlockStateSoft failed for summary %1: %2")
                                      .arg(summary->id)
                                      .arg(errorMessage);
        return;
    }

    summary->recoveryPasswordRequired = state.recoveryRequired;
}

bool NotesRepository::createSchema(QString *errorMessage)
{
    if (!execStatement(database_,
                       QStringLiteral(
                           "CREATE TABLE IF NOT EXISTS notes ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "title TEXT NOT NULL,"
                           "content TEXT NOT NULL DEFAULT '',"
                           "created_at INTEGER NOT NULL,"
                           "updated_at INTEGER NOT NULL,"
                           "theme_id TEXT NOT NULL,"
                           "wrap_mode INTEGER NOT NULL DEFAULT 1,"
                           "font_family TEXT NOT NULL DEFAULT '',"
                           "font_point_size INTEGER NOT NULL DEFAULT 14,"
                           "window_geometry BLOB,"
                           "encrypted INTEGER NOT NULL DEFAULT 0,"
                           "failed_unlock_attempts INTEGER NOT NULL DEFAULT 0,"
                           "recovery_password_required INTEGER NOT NULL DEFAULT 0,"
                           "encryption_version INTEGER NOT NULL DEFAULT 0,"
                           "encrypted_payload BLOB,"
                           "encrypted_payload_nonce BLOB,"
                           "simple_wrap_ciphertext BLOB,"
                           "simple_wrap_nonce BLOB,"
                           "simple_wrap_salt BLOB,"
                           "simple_wrap_opslimit INTEGER,"
                           "simple_wrap_memlimit INTEGER,"
                           "simple_wrap_algorithm INTEGER,"
                           "recovery_wrap_ciphertext BLOB,"
                           "recovery_wrap_nonce BLOB,"
                           "recovery_wrap_salt BLOB,"
                           "recovery_wrap_opslimit INTEGER,"
                           "recovery_wrap_memlimit INTEGER,"
                           "recovery_wrap_algorithm INTEGER"
                           ")"),
                       errorMessage)) {
        return false;
    }

    if (!runMigrations(errorMessage)) {
        return false;
    }

    if (!execStatement(database_,
                       QStringLiteral(
                           "CREATE INDEX IF NOT EXISTS idx_notes_updated_at ON notes(updated_at DESC)"),
                       errorMessage)) {
        return false;
    }

    return execStatement(database_,
                         QStringLiteral(
                             "CREATE INDEX IF NOT EXISTS idx_notes_title ON notes(title COLLATE NOCASE)"),
                         errorMessage);
}

bool NotesRepository::runMigrations(QString *errorMessage)
{
    const QString tableName = QStringLiteral("notes");
    return ensureColumn(database_,
                        tableName,
                        QStringLiteral("encrypted"),
                        QStringLiteral("encrypted INTEGER NOT NULL DEFAULT 0"),
                        errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("failed_unlock_attempts"),
                           QStringLiteral("failed_unlock_attempts INTEGER NOT NULL DEFAULT 0"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_password_required"),
                           QStringLiteral("recovery_password_required INTEGER NOT NULL DEFAULT 0"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("encryption_version"),
                           QStringLiteral("encryption_version INTEGER NOT NULL DEFAULT 0"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("encrypted_payload"),
                           QStringLiteral("encrypted_payload BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("encrypted_payload_nonce"),
                           QStringLiteral("encrypted_payload_nonce BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_ciphertext"),
                           QStringLiteral("simple_wrap_ciphertext BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_nonce"),
                           QStringLiteral("simple_wrap_nonce BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_salt"),
                           QStringLiteral("simple_wrap_salt BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_opslimit"),
                           QStringLiteral("simple_wrap_opslimit INTEGER"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_memlimit"),
                           QStringLiteral("simple_wrap_memlimit INTEGER"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("simple_wrap_algorithm"),
                           QStringLiteral("simple_wrap_algorithm INTEGER"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_ciphertext"),
                           QStringLiteral("recovery_wrap_ciphertext BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_nonce"),
                           QStringLiteral("recovery_wrap_nonce BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_salt"),
                           QStringLiteral("recovery_wrap_salt BLOB"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_opslimit"),
                           QStringLiteral("recovery_wrap_opslimit INTEGER"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_memlimit"),
                           QStringLiteral("recovery_wrap_memlimit INTEGER"),
                           errorMessage)
           && ensureColumn(database_,
                           tableName,
                           QStringLiteral("recovery_wrap_algorithm"),
                           QStringLiteral("recovery_wrap_algorithm INTEGER"),
                           errorMessage);
}

QString NotesRepository::orderClause(SortMode sortMode) const
{
    switch (sortMode) {
    case SortMode::CreatedDesc:
        return QStringLiteral("created_at DESC, id DESC");
    case SortMode::TitleAsc:
        return QStringLiteral("title COLLATE NOCASE ASC, id ASC");
    case SortMode::LastEditedDesc:
    default:
        return QStringLiteral("updated_at DESC, id DESC");
    }
}
