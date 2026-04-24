#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

namespace StoragePaths {

inline QString appDataPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

inline QString assetsRootPath()
{
    return QDir(appDataPath()).filePath(QStringLiteral("assets"));
}

inline QString noteAssetsPath(qint64 noteId)
{
    return QDir(assetsRootPath()).filePath(QString::number(noteId));
}

inline QString legacyBianqianPath()
{
    return QDir::home().filePath(QStringLiteral(".local/share/bianqian"));
}

inline QString legacyBianqianNotesPath()
{
    return QDir(legacyBianqianPath()).filePath(QStringLiteral("notes.json"));
}

} // namespace StoragePaths
