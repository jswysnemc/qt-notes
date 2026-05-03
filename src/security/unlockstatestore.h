#pragma once

#include <QString>
#include <QtGlobal>

struct FailedUnlockState {
    int failedAttempts = 0;
    bool recoveryRequired = false;
};

class UnlockStateStore
{
public:
    bool load(qint64 noteId, FailedUnlockState *state, QString *errorMessage = nullptr) const;
    bool save(qint64 noteId, const FailedUnlockState &state, QString *errorMessage = nullptr) const;
    bool clear(qint64 noteId, QString *errorMessage = nullptr) const;
};
