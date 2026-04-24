#pragma once

#include <QRect>
#include <QVector>
#include <QWidget>

#include "data/notedata.h"
#include "theme/themecatalog.h"

class ApplicationController;
class NoteEditor;
class NoteListPopup;
class QFrame;
class QTimer;
class TitleBar;

class NoteWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NoteWindow(ApplicationController *controller, const NoteData &note, QWidget *parent = nullptr);
    void prepareForDeletion();
    qint64 noteId() const;
    void switchToNote(const NoteData &note);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyTheme();
    void applyEditorSettings();
    void restoreWindowGeometry();
    void applyPendingGeometry();
    void showThemeMenu();
    void showSettingsDialog();
    void showNoteList();
    void openNoteInCurrentWindow(qint64 id);
    void openSelectedNotes(const QVector<qint64> &ids);
    void renameNoteFromList(qint64 id, const QString &title);
    void deleteNotesFromList(const QVector<qint64> &ids);
    void deleteCurrentNote();
    void updateTitle(const QString &title);
    void adjustFontSize(int steps);
    void startWindowMove();
    void flushContent();
    void flushAppearance();
    void flushGeometry();
    void installResizeEventFilters();
    QPoint mapChildPositionToWindow(QWidget *child, const QPointF &position) const;
    void updateResizeCursor(const QPoint &position, QWidget *cursorTarget = nullptr);
    Qt::Edges resizeEdgesAt(const QPoint &position) const;

    ApplicationController *controller_ = nullptr;
    NoteData note_;
    ThemeSpec theme_;
    QFrame *windowSurface_ = nullptr;
    TitleBar *titleBar_ = nullptr;
    NoteEditor *editor_ = nullptr;
    NoteListPopup *noteListPopup_ = nullptr;
    QTimer *contentSaveTimer_ = nullptr;
    QTimer *appearanceSaveTimer_ = nullptr;
    QTimer *geometrySaveTimer_ = nullptr;
    bool contentDirty_ = false;
    bool appearanceDirty_ = false;
    bool geometryDirty_ = false;
    bool deleting_ = false;
    QRect pendingRestoreGeometry_;
    bool pendingRestoreMove_ = false;
    bool initialRestoreApplied_ = false;
};
