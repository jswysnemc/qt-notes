#pragma once

#include <QByteArray>
#include <QPointer>
#include <QTextEdit>

class QContextMenuEvent;
class QDialog;
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
    void setEncryptedAssetKey(const QByteArray &dataKey);
    void setImageAttachmentsEnabled(bool enabled);
    void loadContent(const QString &content);
    QString serializedContent() const;
    bool containsImages() const;
    bool persistImageAttachments(bool encryptedStorage,
                                 QString *content,
                                 QString *errorMessage = nullptr);

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
    void resetTransientState();
    void clearImageResources();
    void deleteImage(const QTextCursor &cursor);
    QSize inlineImageSize(const QSize &imageSize) const;
    QTextCursor imageCursorAt(const QPoint &position) const;
    bool insertImagesFromMimeData(const QMimeData *source);
    bool insertImage(const QImage &image);
    QImage loadImage(const QString &imageUrl) const;
    bool persistRichImageDocument(bool encryptedStorage,
                                  QString *content,
                                  QString *errorMessage);
    void preloadImageResources(const QString &html);
    QImage normalizedImage(const QImage &image, bool *scaled = nullptr) const;
    void showImagePreview(const QImage &image);
    QString storeImage(const QImage &image, bool *scaled = nullptr) const;

    qint64 currentNoteId_ = -1;
    QByteArray encryptedAssetKey_;
    QPointer<QDialog> previewDialog_;
    QPoint pressPosition_;
    int pressedImageCursorPosition_ = -1;
    bool imageAttachmentsEnabled_ = true;
};
