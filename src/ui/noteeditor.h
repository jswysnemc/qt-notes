#pragma once

#include <QTextEdit>

class QMimeData;

class NoteEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit NoteEditor(QWidget *parent = nullptr);
    void setCurrentNoteId(qint64 noteId);
    void loadContent(const QString &content);
    QString serializedContent() const;

signals:
    void fontZoomRequested(int steps);

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    bool insertImagesFromMimeData(const QMimeData *source);
    bool insertImage(const QImage &image);
    QString storeImage(const QImage &image) const;

    qint64 currentNoteId_ = -1;
};
