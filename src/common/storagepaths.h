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

inline QString notePlainAssetPath(qint64 noteId, const QString &assetId)
{
    return QDir(noteAssetsPath(noteId)).filePath(assetId + QStringLiteral(".png"));
}

inline QString noteEncryptedAssetPath(qint64 noteId, const QString &assetId)
{
    return QDir(noteAssetsPath(noteId)).filePath(assetId + QStringLiteral(".enc"));
}

} // namespace StoragePaths
