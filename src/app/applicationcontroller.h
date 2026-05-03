#pragma once

#include <optional>

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QVector>

#include "data/notedata.h"
#include "data/notesrepository.h"

class NoteWindow;

class ApplicationController : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationController(QObject *parent = nullptr);

    bool initialize(QString *errorMessage = nullptr);
    void start();

    void createAndOpenNote();
    void openNote(qint64 id);
    bool deleteNote(qint64 id);
    bool deleteNotes(const QVector<qint64> &ids);
    bool switchWindowToNote(NoteWindow *window, qint64 id);

    std::optional<NoteData> note(qint64 id);
    QVector<NoteSummary> noteSummaries();
    qint64 latestNoteId();
    int openWindowCount() const;
    qint64 startupNoteId();

    bool hasEncryptionPasswordsConfigured() const;
    bool hasCachedEncryptionPasswords() const;
    bool setupGlobalEncryptionPasswords(const QString &simplePassword,
                                        const QString &recoveryPassword,
                                        QString *errorMessage = nullptr);
    bool unlockGlobalEncryptionPasswords(const QString &simplePassword,
                                         const QString &recoveryPassword,
                                         QString *errorMessage = nullptr);
    bool changeSimpleEncryptionPassword(const QString &currentSimplePassword,
                                        const QString &currentRecoveryPassword,
                                        const QString &newSimplePassword,
                                        QString *errorMessage = nullptr);
    bool changeRecoveryEncryptionPassword(const QString &currentSimplePassword,
                                          const QString &currentRecoveryPassword,
                                          const QString &newRecoveryPassword,
                                          QString *errorMessage = nullptr);
    void clearCachedEncryptionPasswords();

    NoteEncryptionResult enableNoteEncryption(qint64 id,
                                              const QString &title,
                                              const QString &content);
    NoteUnlockAttemptResult unlockNoteWithSimplePassword(qint64 id, const QString &password);
    NoteUnlockAttemptResult unlockNoteWithRecoveryPassword(qint64 id, const QString &password);
    bool saveEncryptedNote(qint64 id,
                           const QString &title,
                           const QString &content,
                           const QByteArray &dataKey,
                           QString *errorMessage = nullptr);
    bool disableNoteEncryption(qint64 id,
                               const QString &title,
                               const QString &content,
                               QString *errorMessage = nullptr);

    SortMode sortMode() const;
    void setSortMode(SortMode sortMode);
    StartupNoteMode startupNoteMode() const;
    void setStartupNoteMode(StartupNoteMode mode);
    QString globalFontFamily() const;
    int globalFontPointSize() const;
    void setGlobalFontSettings(const QString &fontFamily, int fontPointSize);
    void rememberClosedNote(qint64 id);

    QStringList recentFonts() const;
    void registerRecentFont(const QString &family);

    void saveTitle(qint64 id, const QString &title);
    void saveContent(qint64 id, const QString &content);
    void saveAppearance(qint64 id, const QString &themeId, bool wrapMode);
    void saveGeometry(qint64 id, const QByteArray &geometry);

signals:
    void notesChanged();
    void noteTitleChanged(qint64 id, const QString &title);
    void globalFontSettingsChanged(const QString &fontFamily, int fontPointSize);

private:
    void openWindowFor(const NoteData &note);
    void removeWindow(NoteWindow *window);
    void cacheSimplePassword(const QString &password);
    void cacheRecoveryPassword(const QString &password);

    NotesRepository repository_;
    QSettings settings_;
    QMap<qint64, QPointer<NoteWindow>> windows_;
    QByteArray cachedSimplePassword_;
    QByteArray cachedRecoveryPassword_;
};
