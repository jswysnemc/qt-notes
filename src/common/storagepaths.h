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

} // namespace StoragePaths
