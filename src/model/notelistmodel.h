#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "data/notedata.h"

class NoteListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CreatedAtRole,
        UpdatedAtRole,
        ThemeIdRole,
    };

    explicit NoteListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setNotes(const QVector<NoteSummary> &notes);
    qint64 noteIdAt(int row) const;
    int rowForNoteId(qint64 id) const;

private:
    QVector<NoteSummary> notes_;
};
