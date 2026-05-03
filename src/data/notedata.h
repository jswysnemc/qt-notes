#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

enum class SortMode {
    LastEditedDesc = 0,
    CreatedDesc = 1,
    TitleAsc = 2,
};

enum class StartupNoteMode {
    LastClosed = 0,
    LastEdited = 1,
    LastCreated = 2,
};

struct NoteData {
    qint64 id = -1;
    QString title;
    QString content;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QString themeId;
    bool wrapMode = true;
    QString fontFamily;
    int fontPointSize = 14;
    QByteArray windowGeometry;
    bool isEncrypted = false;
    int failedUnlockAttempts = 0;
    bool recoveryPasswordRequired = false;
};

struct NoteSummary {
    qint64 id = -1;
    QString title;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QString themeId;
    bool isEncrypted = false;
    bool recoveryPasswordRequired = false;
};

inline QString timestampTitle(qint64 msecsSinceEpoch)
{
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString maskedEncryptedTitle(const QString &title)
{
    const QString normalized = title.trimmed();
    if (normalized.isEmpty()) {
        return QStringLiteral("已加密便签");
    }

    const int totalLength = normalized.size();
    const int keepLength = qMax(1, (totalLength + 1) / 2);
    const int maskLength = qMax(1, totalLength - keepLength);
    return normalized.left(keepLength) + QString(maskLength, QLatin1Char('*'));
}

inline QString newTimestampTitle()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString displayTimestamp(qint64 msecsSinceEpoch)
{
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}
