#pragma once

#include <optional>

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "data/notedata.h"

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
    int importBianqianNotes(const QString &sourceDirectoryPath, QString *errorMessage = nullptr);

    bool updateTitle(qint64 id, const QString &title);
    bool updateContent(qint64 id, const QString &content);
    bool updateAppearance(qint64 id, const QString &themeId, bool wrapMode);
    bool updateGeometry(qint64 id, const QByteArray &geometry);
    bool deleteNote(qint64 id);

    QString databasePath() const;

private:
    bool createSchema(QString *errorMessage);
    QString orderClause(SortMode sortMode) const;

    QSqlDatabase database_;
    QString databasePath_;
};
