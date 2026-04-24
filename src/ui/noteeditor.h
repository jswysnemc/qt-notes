#pragma once

#include <QPointer>
#include <QTextEdit>

class QContextMenuEvent;
class QImage;
class QMimeData;
class QMouseEvent;
class QTextCursor;
class QWidget;

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
    void contextMenuEvent(QContextMenuEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void deleteImage(const QTextCursor &cursor);
    QSize inlineImageSize(const QSize &imageSize) const;
    QTextCursor imageCursorAt(const QPoint &position) const;
    bool insertImagesFromMimeData(const QMimeData *source);
    bool insertImage(const QImage &image);
    QImage loadImage(const QString &imageUrl) const;
    QImage normalizedImage(const QImage &image, bool *scaled = nullptr) const;
    void showImagePreview(const QImage &image, const QPoint &globalPos);
    QString storeImage(const QImage &image, bool *scaled = nullptr) const;

    qint64 currentNoteId_ = -1;
    QPointer<QWidget> previewPopup_;
    QPoint pressPosition_;
    int pressedImageCursorPosition_ = -1;
};
