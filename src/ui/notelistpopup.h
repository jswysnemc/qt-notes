#pragma once

#include <QFrame>
#include <QVector>

#include "data/notedata.h"
#include "theme/themecatalog.h"

class QLabel;
class QListView;
class NoteListModel;
class QPushButton;
class QModelIndex;
class QTimer;

class NoteListPopup : public QFrame
{
    Q_OBJECT

public:
    explicit NoteListPopup(QWidget *parent = nullptr);

    void setNotes(const QVector<NoteSummary> &notes);
    void setTheme(const ThemeSpec &theme);
    void popupBelow(QWidget *anchor);

signals:
    void noteChosenInCurrentWindow(qint64 id);
    void noteChosenInNewWindow(qint64 id);
    void notesChosen(const QVector<qint64> &ids);
    void renameRequested(qint64 id, const QString &title);
    void deleteRequested(const QVector<qint64> &ids);

private:
    void setSelectionModeEnabled(bool enabled);
    QVector<qint64> selectedNoteIds() const;
    void updateActionState();
    void handleItemClicked(const QModelIndex &index);
    void toggleSelectionMode();
    void toggleSelectAllNotes();
    void openNoteInNewWindow(const QPoint &position);
    void openSelectedNotes();
    void requestDeleteSelectedNotes();
    void renameNoteAt(const QModelIndex &index);
    void openNoteInCurrentWindow(qint64 id);

    NoteListModel *model_ = nullptr;
    QListView *listView_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *selectionModeButton_ = nullptr;
    QPushButton *selectAllButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    QTimer *browseClickTimer_ = nullptr;
    ThemeSpec theme_;
    bool selectionMode_ = false;
    qint64 pendingOpenNoteId_ = -1;
};
