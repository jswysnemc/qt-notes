#include "data/notesrepository.h"

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

NoteData noteFromQuery(const QSqlQuery &query)
{
    NoteData note;
    note.id = query.value(QStringLiteral("id")).toLongLong();
    note.title = query.value(QStringLiteral("title")).toString();
    note.content = query.value(QStringLiteral("content")).toString();
    note.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    note.updatedAt = query.value(QStringLiteral("updated_at")).toLongLong();
    note.themeId = query.value(QStringLiteral("theme_id")).toString();
    note.wrapMode = query.value(QStringLiteral("wrap_mode")).toInt() != 0;
    note.fontFamily = query.value(QStringLiteral("font_family")).toString();
    note.fontPointSize = query.value(QStringLiteral("font_point_size")).toInt();
    note.windowGeometry = query.value(QStringLiteral("window_geometry")).toByteArray();
    return note;
}

NoteSummary summaryFromQuery(const QSqlQuery &query)
{
    NoteSummary summary;
    summary.id = query.value(QStringLiteral("id")).toLongLong();
    summary.title = query.value(QStringLiteral("title")).toString();
    summary.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    summary.updatedAt = query.value(QStringLiteral("updated_at")).toLongLong();
    summary.themeId = query.value(QStringLiteral("theme_id")).toString();
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
            *errorMessage = QStringLiteral("无法确定应用数据目录");
        }
        qWarning() << "NotesRepository initialize failed: empty app data path";
        return false;
    }

    QDir directory;
    if (!directory.mkpath(dataDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建数据目录：%1").arg(dataDirectory);
        }
        qWarning() << "NotesRepository initialize failed: cannot create data directory" << dataDirectory;
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

    return createSchema(errorMessage);
}

NoteData NotesRepository::createNote()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO notes "
        "(title, content, created_at, updated_at, theme_id, wrap_mode, font_family, font_point_size, window_geometry) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
    query.prepare(QStringLiteral("SELECT id, title, content, created_at, updated_at, theme_id, wrap_mode, "
                                 "font_family, font_point_size, window_geometry "
                                 "FROM notes WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    return noteFromQuery(query);
}

QVector<NoteSummary> NotesRepository::noteSummaries(SortMode sortMode)
{
    QVector<NoteSummary> notes;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id, title, created_at, updated_at, theme_id FROM notes ORDER BY %1")
                        .arg(orderClause(sortMode)))) {
        return notes;
    }

    while (query.next()) {
        notes.push_back(summaryFromQuery(query));
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

bool NotesRepository::updateTitle(qint64 id, const QString &title)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE notes SET title = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(title);
    query.addBindValue(now);
    query.addBindValue(id);
    return query.exec();
}

bool NotesRepository::updateContent(qint64 id, const QString &content)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE notes SET content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(content);
    query.addBindValue(now);
    query.addBindValue(id);
    return query.exec();
}

bool NotesRepository::updateAppearance(qint64 id, const QString &themeId, bool wrapMode)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE notes "
        "SET theme_id = ?, wrap_mode = ?, updated_at = ? "
        "WHERE id = ?"));
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
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM notes WHERE id = ?"));
    query.addBindValue(id);
    return query.exec();
}

QString NotesRepository::databasePath() const
{
    return databasePath_;
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
                           "window_geometry BLOB"
                           ")"),
                       errorMessage)) {
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
