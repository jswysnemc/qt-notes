#include "model/notelistmodel.h"

NoteListModel::NoteListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NoteListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return notes_.size();
}

QVariant NoteListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= notes_.size()) {
        return {};
    }

    const NoteSummary &note = notes_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return note.title;
    case IdRole:
        return note.id;
    case CreatedAtRole:
        return note.createdAt;
    case UpdatedAtRole:
        return note.updatedAt;
    case ThemeIdRole:
        return note.themeId;
    case EncryptedRole:
        return note.isEncrypted;
    case RecoveryRequiredRole:
        return note.recoveryPasswordRequired;
    default:
        return {};
    }
}

void NoteListModel::setNotes(const QVector<NoteSummary> &notes)
{
    beginResetModel();
    notes_ = notes;
    endResetModel();
}

qint64 NoteListModel::noteIdAt(int row) const
{
    if (row < 0 || row >= notes_.size()) {
        return -1;
    }

    return notes_.at(row).id;
}

int NoteListModel::rowForNoteId(qint64 id) const
{
    for (int row = 0; row < notes_.size(); ++row) {
        if (notes_.at(row).id == id) {
            return row;
        }
    }

    return -1;
}
