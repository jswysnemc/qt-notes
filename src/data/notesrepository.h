#pragma once

#include <optional>

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "data/notedata.h"
#include "security/notecrypto.h"
#include "security/unlockstatestore.h"

struct NoteEncryptionResult {
    bool success = false;
    NoteData note;
    QByteArray dataKey;
    QString errorMessage;
};

struct NoteUnlockAttemptResult {
    NoteCrypto::UnlockStatus status = NoteCrypto::UnlockStatus::InvalidData;
    NoteData note;
    QByteArray dataKey;
    int failedUnlockAttempts = 0;
    int remainingSimpleAttempts = 0;
    QString errorMessage;
};

class NotesRepository
{
public:
    NotesRepository();
    ~NotesRepository();

    bool initialize(QString *errorMessage = nullptr);

    NoteData createNote();
    std::optional<NoteData> noteById(qint64 id);
    QVector<NoteSummary> noteSummaries(SortMode sortMode);
    qint64 startupNoteId();
    qint64 latestCreatedNoteId();

    NoteEncryptionResult enableEncryption(qint64 id,
                                          const QString &title,
                                          const QString &content,
                                          const QString &simplePassword,
                                          const QString &recoveryPassword);
    NoteUnlockAttemptResult unlockWithSimplePassword(qint64 id, const QString &password);
    NoteUnlockAttemptResult unlockWithRecoveryPassword(qint64 id, const QString &password);
    bool rewrapSimplePassword(const QString &oldPassword,
                              const QString &newPassword,
                              QString *errorMessage = nullptr);
    bool rewrapRecoveryPassword(const QString &oldPassword,
                                const QString &newPassword,
                                QString *errorMessage = nullptr);

    bool updateTitle(qint64 id, const QString &title);
    bool updateContent(qint64 id, const QString &content);
    bool updateEncryptedNote(qint64 id,
                             const QString &title,
                             const QString &content,
                             const QByteArray &dataKey,
                             QString *errorMessage = nullptr);
    bool disableEncryption(qint64 id,
                           const QString &title,
                           const QString &content,
                           QString *errorMessage = nullptr);
    bool updateAppearance(qint64 id, const QString &themeId, bool wrapMode);
    bool updateGeometry(qint64 id, const QByteArray &geometry);
    bool deleteNote(qint64 id);

    QString databasePath() const;

private:
    bool createSchema(QString *errorMessage);
    bool runMigrations(QString *errorMessage);
    QString orderClause(SortMode sortMode) const;
    bool loadUnlockStateStrict(qint64 noteId,
                               bool encrypted,
                               FailedUnlockState *state,
                               QString *errorMessage = nullptr) const;
    void applyUnlockStateSoft(NoteData *note) const;
    void applyUnlockStateSoft(NoteSummary *summary) const;

    QSqlDatabase database_;
    QString databasePath_;
    UnlockStateStore unlockStateStore_;
};
