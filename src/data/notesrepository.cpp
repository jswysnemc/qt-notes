#include "data/notesrepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>
#include <QVariant>

#include "common/storagepaths.h"
#include "theme/themecatalog.h"

namespace {

constexpr auto kConnectionName = "qt-notes-main-connection";
constexpr auto kDefaultThemeId = "paper";
constexpr auto kDefaultFontSize = 14;
const QString kRichContentPrefix = QStringLiteral("<!--qt-notes:rich-->\n");
constexpr auto kGeometryFormat = "qt-notes-window-v1";

struct ImportedImageData {
    QString url;
    QSize size;
};

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

qint64 normalizedLegacyTimestamp(qint64 value)
{
    if (value <= 0) {
        return 0;
    }

    if (value < 100000000000LL) {
        return value * 1000;
    }
    return value;
}

qint64 earliestPositiveTimestamp(qint64 left, qint64 right)
{
    if (left <= 0) {
        return right;
    }
    if (right <= 0) {
        return left;
    }
    return qMin(left, right);
}

QString themeIdForLegacyColorIndex(int colorIndex)
{
    const QVector<ThemeSpec> &themes = ThemeCatalog::themes();
    if (themes.isEmpty()) {
        return QString::fromLatin1(kDefaultThemeId);
    }

    const int normalizedIndex = qBound(0, colorIndex, themes.size() - 1);
    return themes.at(normalizedIndex).id;
}

QByteArray serializeImportedGeometry(const QRect &rect)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kGeometryFormat));
    root.insert(QStringLiteral("platform"), QGuiApplication::platformName());
    root.insert(QStringLiteral("x"), rect.x());
    root.insert(QStringLiteral("y"), rect.y());
    root.insert(QStringLiteral("width"), rect.width());
    root.insert(QStringLiteral("height"), rect.height());
    root.insert(QStringLiteral("screen"), QString());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString extractHtmlBody(const QString &html)
{
    static const QRegularExpression bodyPattern(
        QStringLiteral("<body[^>]*>([\\s\\S]*)</body>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bodyPattern.match(html);
    return match.hasMatch() ? match.captured(1).trimmed() : html.trimmed();
}

QSize importedInlineImageSize(const QSize &imageSize, int noteWidth, int noteHeight)
{
    if (!imageSize.isValid()) {
        return QSize(240, 160);
    }

    const QSize limit(qBound(280, noteWidth - 28, 640), qBound(180, noteHeight - 40, 420));
    return imageSize.scaled(limit, Qt::KeepAspectRatio);
}

std::optional<ImportedImageData> importLegacyImage(const QDir &sourceDirectory,
                                                   const QString &relativePath,
                                                   qint64 noteId)
{
    if (relativePath.trimmed().isEmpty()) {
        return std::nullopt;
    }

    const QString sourcePath = sourceDirectory.filePath(relativePath);
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return std::nullopt;
    }

    const QString destinationDirectoryPath = StoragePaths::noteAssetsPath(noteId);
    QDir destinationDirectory;
    if (!destinationDirectory.mkpath(destinationDirectoryPath)) {
        return std::nullopt;
    }

    QString destinationPath = QDir(destinationDirectoryPath).filePath(sourceInfo.fileName());
    if (QFileInfo::exists(destinationPath)) {
        const QString baseName = sourceInfo.completeBaseName();
        const QString suffix = sourceInfo.completeSuffix();
        int sequence = 1;
        do {
            const QString candidateName =
                suffix.isEmpty() ? QStringLiteral("%1-%2").arg(baseName).arg(sequence)
                                 : QStringLiteral("%1-%2.%3").arg(baseName).arg(sequence).arg(suffix);
            destinationPath = QDir(destinationDirectoryPath).filePath(candidateName);
            ++sequence;
        } while (QFileInfo::exists(destinationPath));
    }

    if (!QFile::copy(sourcePath, destinationPath)) {
        return std::nullopt;
    }

    QImageReader reader(destinationPath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return std::nullopt;
    }

    return ImportedImageData{ QUrl::fromLocalFile(destinationPath).toString(), image.size() };
}

QString buildImportedContent(const QJsonObject &legacyNote,
                            const QDir &sourceDirectory,
                            qint64 noteId)
{
    const QJsonArray blocks = legacyNote.value(QStringLiteral("blocks")).toArray();
    bool hasImageBlock = false;
    for (const QJsonValue &value : blocks) {
        if (value.isObject() && value.toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("image")) {
            hasImageBlock = true;
            break;
        }
    }

    if (!hasImageBlock) {
        return legacyNote.value(QStringLiteral("content")).toString();
    }

    QTextDocument document;
    QTextCursor cursor(&document);
    const int noteWidth = legacyNote.value(QStringLiteral("width")).toInt(420);
    const int noteHeight = legacyNote.value(QStringLiteral("height")).toInt(520);

    for (const QJsonValue &value : blocks) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject block = value.toObject();
        const QString kind = block.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("text")) {
            cursor.insertText(block.value(QStringLiteral("text")).toString());
            continue;
        }

        if (kind != QStringLiteral("image")) {
            continue;
        }

        const QString relativePath = block.value(QStringLiteral("path")).toString();
        const std::optional<ImportedImageData> importedImage =
            importLegacyImage(sourceDirectory, relativePath, noteId);
        if (!importedImage.has_value()) {
            cursor.insertText(QStringLiteral("[图片迁移失败: %1]").arg(QFileInfo(relativePath).fileName()));
            continue;
        }

        if (!cursor.atBlockStart() || !cursor.block().text().isEmpty()) {
            cursor.insertBlock();
        }

        const QSize displaySize = importedInlineImageSize(importedImage->size, noteWidth, noteHeight);
        QTextImageFormat format;
        format.setName(importedImage->url);
        format.setWidth(displaySize.width());
        format.setHeight(displaySize.height());
        cursor.insertImage(format);
        cursor.insertBlock();
    }

    return kRichContentPrefix + extractHtmlBody(document.toHtml());
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

int NotesRepository::importBianqianNotes(const QString &sourceDirectoryPath, QString *errorMessage)
{
    const QDir sourceDirectory(sourceDirectoryPath);
    const QString notesPath = sourceDirectory.filePath(QStringLiteral("notes.json"));
    QFile sourceFile(notesPath);
    if (!sourceFile.exists()) {
        return 0;
    }

    if (!sourceFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取旧便签数据：%1").arg(notesPath);
        }
        return -1;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(sourceFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("旧便签数据格式无效：%1").arg(parseError.errorString());
        }
        return -1;
    }

    const QJsonArray notes = document.array();
    if (notes.isEmpty()) {
        return 0;
    }

    if (!database_.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法开始迁移事务：%1").arg(database_.lastError().text());
        }
        return -1;
    }

    int importedCount = 0;
    for (const QJsonValue &value : notes) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject legacyNote = value.toObject();
        const qint64 editedAt = normalizedLegacyTimestamp(
            legacyNote.value(QStringLiteral("last_edited")).toVariant().toLongLong());
        const qint64 activeAt = normalizedLegacyTimestamp(
            legacyNote.value(QStringLiteral("last_active")).toVariant().toLongLong());
        qint64 updatedAt = qMax(editedAt, activeAt);
        if (updatedAt <= 0) {
            updatedAt = QDateTime::currentMSecsSinceEpoch();
        }

        qint64 createdAt = earliestPositiveTimestamp(editedAt, activeAt);
        if (createdAt <= 0) {
            createdAt = updatedAt;
        }

        QString title = legacyNote.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) {
            title = timestampTitle(updatedAt);
        }

        const int width = qMax(280, legacyNote.value(QStringLiteral("width")).toInt(420));
        const int height = qMax(220, legacyNote.value(QStringLiteral("height")).toInt(520));
        const QRect geometryRect(legacyNote.value(QStringLiteral("x")).toInt(),
                                 legacyNote.value(QStringLiteral("y")).toInt(),
                                 width,
                                 height);
        const QByteArray geometry = serializeImportedGeometry(geometryRect);
        const QString themeId =
            themeIdForLegacyColorIndex(legacyNote.value(QStringLiteral("color_index")).toInt());
        const bool wrapMode = legacyNote.value(QStringLiteral("wrap")).toBool(true);
        const int fontPointSize = qBound(10,
                                         qRound(legacyNote.value(QStringLiteral("font_size"))
                                                    .toDouble(kDefaultFontSize)),
                                         40);

        QSqlQuery insertQuery(database_);
        insertQuery.prepare(QStringLiteral(
            "INSERT INTO notes "
            "(title, content, created_at, updated_at, theme_id, wrap_mode, font_family, font_point_size, window_geometry) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insertQuery.addBindValue(title);
        insertQuery.addBindValue(QStringLiteral(""));
        insertQuery.addBindValue(createdAt);
        insertQuery.addBindValue(updatedAt);
        insertQuery.addBindValue(themeId);
        insertQuery.addBindValue(wrapMode ? 1 : 0);
        insertQuery.addBindValue(QStringLiteral(""));
        insertQuery.addBindValue(fontPointSize);
        insertQuery.addBindValue(geometry);
        if (!insertQuery.exec()) {
            database_.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("迁移旧便签失败：%1").arg(insertQuery.lastError().text());
            }
            return -1;
        }

        const qint64 newNoteId = insertQuery.lastInsertId().toLongLong();
        const QString importedContent = buildImportedContent(legacyNote, sourceDirectory, newNoteId);

        QSqlQuery updateQuery(database_);
        updateQuery.prepare(QStringLiteral("UPDATE notes SET content = ? WHERE id = ?"));
        updateQuery.addBindValue(importedContent);
        updateQuery.addBindValue(newNoteId);
        if (!updateQuery.exec()) {
            database_.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("写入迁移便签内容失败：%1").arg(updateQuery.lastError().text());
            }
            return -1;
        }

        ++importedCount;
    }

    if (!database_.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("提交旧便签迁移事务失败：%1").arg(database_.lastError().text());
        }
        database_.rollback();
        return -1;
    }

    return importedCount;
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
