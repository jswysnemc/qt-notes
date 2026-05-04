#include "ui/noteeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFileDevice>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QMouseEvent>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>
#include <QtMath>

#include <sodium.h>

#include "common/storagepaths.h"
#include "security/notecrypto.h"

namespace {

const QString kRichContentPrefix = QStringLiteral("<!--qt-notes:rich-->\n");
const QString kNoteAssetScheme = QStringLiteral("note-asset");
constexpr qint64 kMaxSourceImageBytes = 20LL * 1024 * 1024;
constexpr qint64 kMaxImageMemoryBytes = 12LL * 1024 * 1024;
constexpr int kMaxStoredImageExtent = 2200;
constexpr int kPreviewMargin = 36;
constexpr double kPreviewScreenRatio = 0.9;

QUrl noteAssetUrl(const QString &assetId)
{
    return QUrl(kNoteAssetScheme + QStringLiteral("://") + assetId);
}

bool isNoteAssetUrl(const QString &imageName, QString *assetId = nullptr)
{
    const QUrl url(imageName);
    if (url.scheme() != kNoteAssetScheme) {
        return false;
    }

    QString resolvedAssetId = url.host().trimmed();
    if (resolvedAssetId.isEmpty()) {
        resolvedAssetId = url.path().trimmed();
        if (resolvedAssetId.startsWith(QLatin1Char('/'))) {
            resolvedAssetId.remove(0, 1);
        }
    }
    if (resolvedAssetId.isEmpty()) {
        return false;
    }

    if (assetId != nullptr) {
        *assetId = resolvedAssetId;
    }
    return true;
}

const QRegularExpression &urlPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\b((?:https?|ftp)://[^\s<>"']+|www\.[^\s<>"']+|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}))"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QUrl normalizedExternalUrl(QString text)
{
    while (!text.isEmpty() && QStringLiteral(".,;:!?)]}").contains(text.back())) {
        text.chop(1);
    }

    if (text.contains(QLatin1Char('@')) && !text.contains(QStringLiteral("://"))) {
        return QUrl(QStringLiteral("mailto:") + text);
    }
    if (text.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        return QUrl(QStringLiteral("https://") + text);
    }
    return QUrl(text);
}

bool documentContainsImages(const QTextDocument *document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat()) {
                return true;
            }
        }
    }

    return false;
}

QString extractHtmlBody(const QString &html)
{
    static const QRegularExpression bodyPattern(
        QStringLiteral("<body[^>]*>([\\s\\S]*)</body>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bodyPattern.match(html);
    return match.hasMatch() ? match.captured(1).trimmed() : html.trimmed();
}

bool isImageFileUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return false;
    }

    QImageReader reader(url.toLocalFile());
    return reader.canRead();
}

QImage imageFromMimeData(const QMimeData *source)
{
    if (source == nullptr || !source->hasImage()) {
        return {};
    }

    const QVariant imageData = source->imageData();
    if (imageData.canConvert<QImage>()) {
        return qvariant_cast<QImage>(imageData);
    }
    if (imageData.canConvert<QPixmap>()) {
        return qvariant_cast<QPixmap>(imageData).toImage();
    }
    return {};
}

QImage readSourceImageFromFile(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || fileInfo.size() <= 0 || fileInfo.size() > kMaxSourceImageBytes) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    return reader.read();
}

QImage readStoredImageFromFile(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || fileInfo.size() <= 0) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    return reader.read();
}

QString localFilePathFromImageName(const QString &imageName)
{
    const QUrl url(imageName);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return imageName;
}

bool writeBytesAtomically(const QString &filePath, const QByteArray &bytes)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        return false;
    }
    QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool writeImageAsPng(const QString &filePath, const QImage &image)
{
    if (image.isNull()) {
        return false;
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return false;
    }
    return writeBytesAtomically(filePath, bytes);
}

} // namespace

NoteEditor::NoteEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setPlaceholderText(tr("Start writing..."));
    setAcceptRichText(false);
    setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTabStopDistance(28.0);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    connect(this, &QTextEdit::textChanged, this, &NoteEditor::updateUrlHighlights);
}

void NoteEditor::setCurrentNoteId(qint64 noteId)
{
    currentNoteId_ = noteId;
}

void NoteEditor::setEncryptedAssetKey(const QByteArray &dataKey)
{
    NoteCrypto::wipe(&encryptedAssetKey_);
    encryptedAssetKey_ = dataKey;
}

void NoteEditor::setImageAttachmentsEnabled(bool enabled)
{
    imageAttachmentsEnabled_ = enabled;
}

void NoteEditor::loadContent(const QString &content)
{
    resetTransientState();
    clearImageResources();
    clear();
    if (content.startsWith(kRichContentPrefix)) {
        const QString html = content.mid(kRichContentPrefix.size());
        preloadImageResources(html);
        setHtml(html);
        updateUrlHighlights();
        return;
    }

    setPlainText(content);
    updateUrlHighlights();
}

void NoteEditor::resetTransientState()
{
    pressedImageCursorPosition_ = -1;
    pressPosition_ = {};
    if (!previewDialog_.isNull()) {
        previewDialog_->close();
    }
}

void NoteEditor::clearImageResources()
{
    document()->clear();
}

QString NoteEditor::serializedContent() const
{
    if (!documentContainsImages(document())) {
        return toPlainText();
    }

    return kRichContentPrefix + extractHtmlBody(toHtml());
}

bool NoteEditor::containsImages() const
{
    return documentContainsImages(document());
}

bool NoteEditor::persistImageAttachments(bool encryptedStorage,
                                         QString *content,
                                         QString *errorMessage)
{
    if (content == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Internal error: missing note content output");
        }
        return false;
    }

    if (!documentContainsImages(document())) {
        *content = toPlainText();

        if (currentNoteId_ >= 0) {
            QDir directory(StoragePaths::noteAssetsPath(currentNoteId_));
            const QStringList filters = {QStringLiteral("*.png"), QStringLiteral("*.enc")};
            for (const QFileInfo &fileInfo :
                 directory.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot)) {
                QFile::remove(fileInfo.filePath());
            }
        }
        return true;
    }

    return persistRichImageDocument(encryptedStorage, content, errorMessage);
}

bool NoteEditor::canInsertFromMimeData(const QMimeData *source) const
{
    if (source == nullptr) {
        return false;
    }

    if (source->hasImage()) {
        return true;
    }

    const QList<QUrl> urls = source->urls();
    for (const QUrl &url : urls) {
        if (isImageFileUrl(url)) {
            return true;
        }
    }

    if (source->hasText()) {
        return true;
    }

    return QTextEdit::canInsertFromMimeData(source);
}

void NoteEditor::changeEvent(QEvent *event)
{
    QTextEdit::changeEvent(event);

    if (event->type() == QEvent::PaletteChange) {
        updateUrlHighlights();
    }
}

void NoteEditor::contextMenuEvent(QContextMenuEvent *event)
{
    const QTextCursor imageCursor = imageCursorAt(event->pos());
    const QUrl linkUrl = linkAtPosition(event->pos());
    const QTextCursor currentCursor = textCursor();
    const bool hasImage = !imageCursor.isNull();
    const bool hasLink = linkUrl.isValid();
    const bool hasSelection = currentCursor.hasSelection();
    const bool readOnly = isReadOnly();
    const bool canUndo = document()->isUndoAvailable();
    const bool canRedo = document()->isRedoAvailable();
    const bool canCutSelection = hasSelection && isUndoRedoEnabled() && !readOnly;
    const bool canCopySelection = hasSelection;
    const bool canPasteContent = canPaste() && !readOnly;
    const bool canDeleteSelection = hasSelection && !readOnly;
    const bool canSelectAllContent = !document()->isEmpty();
    const QPalette menuPalette = palette();
    const QColor background = menuPalette.color(QPalette::Base);
    const QColor text = menuPalette.color(QPalette::Text);
    const QColor border = menuPalette.color(QPalette::Mid);
    const QColor hover = menuPalette.color(QPalette::Highlight);
    const QColor hoverText = menuPalette.color(QPalette::HighlightedText);
    const int disabledGray = background.lightness() < 128 ? 140 : 150;
    const QColor disabledText(disabledGray, disabledGray, disabledGray);
    const QString backgroundColor = background.name();
    const QString textColor = text.name();
    const QString borderColor = border.name();
    const QString hoverColor = hover.name();
    const QString hoverTextColor = hoverText.name();
    const QString disabledTextColor = disabledText.name();

    QMenu menu;
    menu.setStyleSheet(QStringLiteral(R"(
QMenu {
    background: %1;
    color: %2;
    border: 1px solid %3;
    padding: 6px;
}
QMenu::item {
    padding: 6px 26px 6px 10px;
    margin: 1px 0;
    border-radius: 6px;
    background: transparent;
}
QMenu::item:selected {
    background: %4;
    color: %5;
}
QMenu::item:disabled {
    color: %6;
}
QMenu::separator {
    height: 1px;
    margin: 6px 8px;
    background: %3;
}
)")
                           .arg(backgroundColor,
                                textColor,
                                borderColor,
                                hoverColor,
                                hoverTextColor,
                                disabledTextColor));

    QAction *previewAction = nullptr;
    QAction *copyImageAction = nullptr;
    QAction *deleteImageAction = nullptr;
    QAction *openLinkAction = nullptr;
    if (hasLink) {
        openLinkAction = menu.addAction(tr("Open link"));
        menu.addSeparator();
    }

    if (hasImage) {
        previewAction = menu.addAction(tr("Preview image"));
        copyImageAction = menu.addAction(tr("Copy image"));
        deleteImageAction = menu.addAction(tr("Delete image"));
        menu.addSeparator();
    }

    QAction *undoAction = menu.addAction(tr("Undo"));
    undoAction->setEnabled(canUndo);

    QAction *redoAction = menu.addAction(tr("Redo"));
    redoAction->setEnabled(canRedo);

    menu.addSeparator();

    QAction *cutAction = menu.addAction(tr("Cut"));
    cutAction->setEnabled(canCutSelection);

    QAction *copyAction = menu.addAction(tr("Copy"));
    copyAction->setEnabled(canCopySelection);

    QAction *pasteAction = menu.addAction(tr("Paste"));
    pasteAction->setEnabled(canPasteContent);

    QAction *deleteSelectionAction = menu.addAction(tr("Delete"));
    deleteSelectionAction->setEnabled(canDeleteSelection);

    menu.addSeparator();

    QAction *selectAllAction = menu.addAction(tr("Select all"));
    selectAllAction->setEnabled(canSelectAllContent);
    QAction *selected = menu.exec(event->globalPos());
    if (selected == nullptr) {
        return;
    }

    if (selected == openLinkAction) {
        openUrl(linkUrl);
        return;
    }

    if (selected == previewAction) {
        const QImage image = loadImage(imageCursor.charFormat().toImageFormat().name());
        if (!image.isNull()) {
            showImagePreview(image);
        }
        return;
    }

    if (selected == copyImageAction) {
        const QImage image = loadImage(imageCursor.charFormat().toImageFormat().name());
        if (!image.isNull()) {
            QApplication::clipboard()->setImage(image);
        }
        return;
    }

    if (selected == deleteImageAction) {
        deleteImage(imageCursor);
        return;
    }

    if (selected == undoAction) {
        undo();
        return;
    }

    if (selected == redoAction) {
        redo();
        return;
    }

    if (selected == cutAction) {
        cut();
        return;
    }

    if (selected == copyAction) {
        copy();
        return;
    }

    if (selected == pasteAction) {
        paste();
        return;
    }

    if (selected == deleteSelectionAction) {
        QTextCursor cursor = textCursor();
        cursor.removeSelectedText();
        setTextCursor(cursor);
        return;
    }

    if (selected == selectAllAction) {
        selectAll();
    }
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    if (insertImagesFromMimeData(source)) {
        return;
    }

    if (source != nullptr && source->hasText()) {
        textCursor().insertText(source->text());
        return;
    }

    QTextEdit::insertFromMimeData(source);
}

void NoteEditor::mousePressEvent(QMouseEvent *event)
{
    pressPosition_ = event->position().toPoint();
    pressedImageCursorPosition_ = -1;

    if (event->button() == Qt::LeftButton) {
        const QTextCursor imageCursor = imageCursorAt(event->position().toPoint());
        if (!imageCursor.isNull()) {
            pressedImageCursorPosition_ = imageCursor.selectionStart();
        }
    }

    QTextEdit::mousePressEvent(event);
}

void NoteEditor::mouseMoveEvent(QMouseEvent *event)
{
    QTextEdit::mouseMoveEvent(event);

    const QUrl linkUrl = linkAtPosition(event->position().toPoint());
    if (linkUrl.isValid()) {
        viewport()->setCursor(Qt::PointingHandCursor);
        return;
    }

    viewport()->setCursor(Qt::IBeamCursor);
}

void NoteEditor::mouseReleaseEvent(QMouseEvent *event)
{
    const QUrl linkUrl = linkAtPosition(event->position().toPoint());
    if (event->button() == Qt::LeftButton && linkUrl.isValid()
        && (event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::MetaModifier))) {
        openUrl(linkUrl);
        pressedImageCursorPosition_ = -1;
        event->accept();
        return;
    }

    QTextEdit::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton || pressedImageCursorPosition_ < 0) {
        pressedImageCursorPosition_ = -1;
        return;
    }

    if ((event->position().toPoint() - pressPosition_).manhattanLength()
        > QApplication::startDragDistance()) {
        pressedImageCursorPosition_ = -1;
        return;
    }

    const QTextCursor imageCursor = imageCursorAt(event->position().toPoint());
    if (imageCursor.isNull() || imageCursor.selectionStart() != pressedImageCursorPosition_) {
        pressedImageCursorPosition_ = -1;
        return;
    }

    const QImage image = loadImage(imageCursor.charFormat().toImageFormat().name());
    if (!image.isNull()) {
        showImagePreview(image);
    }

    pressedImageCursorPosition_ = -1;
}

void NoteEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            emit fontZoomRequested(delta > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }

    QTextEdit::wheelEvent(event);
}

bool NoteEditor::insertImagesFromMimeData(const QMimeData *source)
{
    if (source == nullptr) {
        return false;
    }

    if (!imageAttachmentsEnabled_) {
        bool containsImageInput = source->hasImage();
        if (!containsImageInput) {
            const QList<QUrl> urls = source->urls();
            for (const QUrl &url : urls) {
                if (isImageFileUrl(url)) {
                    containsImageInput = true;
                    break;
                }
            }
        }

        if (containsImageInput) {
            QMessageBox::information(window(),
                                     tr("This note is encrypted"),
                                     tr("Encrypted notes do not support inserting image attachments."));
            return true;
        }
    }

    const QImage clipboardImage = imageFromMimeData(source);
    if (!clipboardImage.isNull()) {
        return insertImage(clipboardImage);
    }

    bool inserted = false;
    const QList<QUrl> urls = source->urls();
    for (const QUrl &url : urls) {
        if (!isImageFileUrl(url)) {
            continue;
        }

        const QImage image = readSourceImageFromFile(url.toLocalFile());
        if (image.isNull()) {
            continue;
        }

        inserted = insertImage(image) || inserted;
    }

    return inserted;
}

bool NoteEditor::insertImage(const QImage &image)
{
    if (image.isNull()) {
        return false;
    }

    bool scaled = false;
    const QImage normalized = normalizedImage(image, &scaled);
    const QString imageUrl = storeImage(normalized, &scaled);
    if (imageUrl.isEmpty()) {
        return false;
    }

    document()->addResource(QTextDocument::ImageResource, QUrl(imageUrl), normalized);
    const QSize displaySize = inlineImageSize(normalized.size());

    QTextCursor cursor = textCursor();
    if (!cursor.atBlockStart() || !cursor.block().text().isEmpty()) {
        cursor.insertBlock();
    }

    QTextImageFormat format;
    format.setName(imageUrl);
    format.setWidth(displaySize.width());
    format.setHeight(displaySize.height());
    cursor.insertImage(format);
    cursor.insertBlock();
    setTextCursor(cursor);
    return true;
}

void NoteEditor::deleteImage(const QTextCursor &cursor)
{
    if (cursor.isNull() || isReadOnly()) {
        return;
    }

    QTextCursor blockCursor = cursor;
    blockCursor.beginEditBlock();
    blockCursor.select(QTextCursor::BlockUnderCursor);
    blockCursor.removeSelectedText();
    blockCursor.deleteChar();
    blockCursor.endEditBlock();
}

QSize NoteEditor::inlineImageSize(const QSize &imageSize) const
{
    if (!imageSize.isValid()) {
        return QSize(240, 160);
    }

    const QSize limit(qBound(280, viewport()->width() - 28, 640),
                      qBound(180, viewport()->height() - 40, 420));
    return imageSize.scaled(limit, Qt::KeepAspectRatio);
}

QTextCursor NoteEditor::imageCursorAt(const QPoint &position) const
{
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    if (layout == nullptr) {
        return {};
    }

    QPointF documentPosition(position);
    if (horizontalScrollBar() != nullptr) {
        documentPosition.rx() += horizontalScrollBar()->value();
    }
    if (verticalScrollBar() != nullptr) {
        documentPosition.ry() += verticalScrollBar()->value();
    }

    const int hitPosition = layout->hitTest(documentPosition, Qt::ExactHit);
    if (hitPosition < 0) {
        return {};
    }

    const int charCount = document()->characterCount();
    const int positions[2] = {hitPosition, hitPosition - 1};

    for (int candidate : positions) {
        if (candidate < 0 || candidate >= charCount - 1) {
            continue;
        }

        QTextCursor cursor(document());
        cursor.setPosition(candidate);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (cursor.charFormat().isImageFormat()) {
            return cursor;
        }
    }

    return {};
}

QImage NoteEditor::loadImage(const QString &imageUrl) const
{
    const QVariant resource = document()->resource(QTextDocument::ImageResource, QUrl(imageUrl));
    if (resource.canConvert<QImage>()) {
        return qvariant_cast<QImage>(resource);
    }
    if (resource.canConvert<QPixmap>()) {
        return qvariant_cast<QPixmap>(resource).toImage();
    }

    QString assetId;
    if (isNoteAssetUrl(imageUrl, &assetId)) {
        if (currentNoteId_ < 0) {
            return {};
        }

        if (!encryptedAssetKey_.isEmpty()) {
            QFile encryptedFile(StoragePaths::noteEncryptedAssetPath(currentNoteId_, assetId));
            if (!encryptedFile.open(QIODevice::ReadOnly)) {
                return {};
            }

            const QByteArray packed = encryptedFile.readAll();
            if (packed.size() <= crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) {
                return {};
            }

            const QByteArray nonce =
                packed.left(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
            const QByteArray ciphertext =
                packed.mid(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
            QByteArray plaintext;
            if (!NoteCrypto::decryptAttachmentBytes(currentNoteId_,
                                                    assetId,
                                                    ciphertext,
                                                    nonce,
                                                    encryptedAssetKey_,
                                                    &plaintext,
                                                    nullptr)) {
                return {};
            }

            const QImage image = QImage::fromData(plaintext, "PNG");
            NoteCrypto::wipe(&plaintext);
            return image;
        }

        return readStoredImageFromFile(StoragePaths::notePlainAssetPath(currentNoteId_, assetId));
    }

    return readStoredImageFromFile(localFilePathFromImageName(imageUrl));
}

QUrl NoteEditor::linkAtPosition(const QPoint &position) const
{
    const QTextCursor cursor = cursorForPosition(position);
    const int cursorPosition = cursor.position();
    const QString text = toPlainText();
    QRegularExpressionMatchIterator matches = urlPattern().globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int start = match.capturedStart(1);
        const int end = match.capturedEnd(1);
        if (cursorPosition < start || cursorPosition > end) {
            continue;
        }

        return normalizedExternalUrl(match.captured(1));
    }

    return {};
}

void NoteEditor::openUrl(const QUrl &url) const
{
    if (!url.isValid()) {
        return;
    }

    QDesktopServices::openUrl(url);
}

bool NoteEditor::persistRichImageDocument(bool encryptedStorage,
                                          QString *content,
                                          QString *errorMessage)
{
    if (currentNoteId_ < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Invalid note identifier");
        }
        return false;
    }

    if (encryptedStorage && encryptedAssetKey_.size()
                                != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Invalid attachment encryption key");
        }
        return false;
    }

    const QString html = extractHtmlBody(toHtml());
    QTextDocument tempDocument;
    tempDocument.setHtml(html);

    QHash<QString, QString> normalizedNames;
    QSet<QString> referencedAssetIds;
    QDir assetDirectory(StoragePaths::noteAssetsPath(currentNoteId_));
    if (!assetDirectory.exists() && !QDir().mkpath(assetDirectory.path())) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Failed to create note attachment directory");
        }
        return false;
    }

    for (QTextBlock block = tempDocument.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }

            QTextImageFormat imageFormat = fragment.charFormat().toImageFormat();
            const QString originalName = imageFormat.name();

            QString normalizedName = normalizedNames.value(originalName);
            QString assetId;
            if (normalizedName.isEmpty()) {
                if (!isNoteAssetUrl(originalName, &assetId)) {
                    assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                }

                const QImage image = loadImage(originalName);
                if (image.isNull()) {
                    if (errorMessage != nullptr) {
                        *errorMessage = tr("Failed to read image attachment: %1").arg(originalName);
                    }
                    return false;
                }

                if (encryptedStorage) {
                    QByteArray pngBytes;
                    QBuffer buffer(&pngBytes);
                    buffer.open(QIODevice::WriteOnly);
                    if (!image.save(&buffer, "PNG")) {
                        if (errorMessage != nullptr) {
                            *errorMessage = tr("Failed to encode image attachment");
                        }
                        return false;
                    }

                    QByteArray ciphertext;
                    QByteArray nonce;
                    if (!NoteCrypto::encryptAttachmentBytes(currentNoteId_,
                                                            assetId,
                                                            pngBytes,
                                                            encryptedAssetKey_,
                                                            &ciphertext,
                                                            &nonce,
                                                            errorMessage)) {
                        return false;
                    }

                    if (!writeBytesAtomically(StoragePaths::noteEncryptedAssetPath(currentNoteId_,
                                                                                   assetId),
                                              nonce + ciphertext)) {
                        if (errorMessage != nullptr) {
                            *errorMessage = tr("Failed to write encrypted image attachment");
                        }
                        return false;
                    }
                } else if (!writeImageAsPng(StoragePaths::notePlainAssetPath(currentNoteId_, assetId),
                                            image)) {
                    if (errorMessage != nullptr) {
                        *errorMessage = tr("Failed to write image attachment");
                    }
                    return false;
                }

                normalizedName = noteAssetUrl(assetId).toString();
                normalizedNames.insert(originalName, normalizedName);
            } else {
                isNoteAssetUrl(normalizedName, &assetId);
            }

            referencedAssetIds.insert(assetId);
            if (normalizedName != originalName) {
                QTextCursor cursor(&tempDocument);
                cursor.setPosition(fragment.position());
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                imageFormat.setName(normalizedName);
                cursor.setCharFormat(imageFormat);
            }
        }
    }

    const QStringList filters = {QStringLiteral("*.png"), QStringLiteral("*.enc")};
    for (const QFileInfo &fileInfo :
         assetDirectory.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot)) {
        const QString suffix = fileInfo.suffix().toLower();
        const QString baseName = fileInfo.completeBaseName();
        if (!referencedAssetIds.contains(baseName)
            || (encryptedStorage && suffix == QStringLiteral("png"))
            || (!encryptedStorage && suffix == QStringLiteral("enc"))) {
            QFile::remove(fileInfo.filePath());
        }
    }

    *content = kRichContentPrefix + extractHtmlBody(tempDocument.toHtml());
    return true;
}

void NoteEditor::preloadImageResources(const QString &html)
{
    QTextDocument probe;
    probe.setHtml(html);

    for (QTextBlock block = probe.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }

            const QString imageName = fragment.charFormat().toImageFormat().name();
            if (!isNoteAssetUrl(imageName)) {
                continue;
            }

            const QImage image = loadImage(imageName);
            if (!image.isNull()) {
                document()->addResource(QTextDocument::ImageResource, QUrl(imageName), image);
            }
        }
    }
}

void NoteEditor::updateUrlHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = toPlainText();
    QRegularExpressionMatchIterator matches = urlPattern().globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(match.capturedStart(1));
        selection.cursor.setPosition(match.capturedEnd(1), QTextCursor::KeepAnchor);
        selection.format.setForeground(palette().color(QPalette::Link));
        selection.format.setFontUnderline(true);
        selections.append(selection);
    }

    setExtraSelections(selections);
}

QImage NoteEditor::normalizedImage(const QImage &image, bool *scaled) const
{
    if (scaled != nullptr) {
        *scaled = false;
    }

    if (image.isNull()) {
        return {};
    }

    QImage normalized = image;
    auto scaleDown = [&](double factor) {
        const int targetWidth = qMax(1, int(normalized.width() * factor));
        const int targetHeight = qMax(1, int(normalized.height() * factor));
        normalized = normalized.scaled(targetWidth,
                                       targetHeight,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        if (scaled != nullptr) {
            *scaled = true;
        }
    };

    if (normalized.width() > kMaxStoredImageExtent || normalized.height() > kMaxStoredImageExtent) {
        const QSize scaledSize = normalized.size().scaled(kMaxStoredImageExtent,
                                                          kMaxStoredImageExtent,
                                                          Qt::KeepAspectRatio);
        normalized = normalized.scaled(scaledSize,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        if (scaled != nullptr) {
            *scaled = true;
        }
    }

    while (normalized.sizeInBytes() > kMaxImageMemoryBytes && normalized.width() > 1
           && normalized.height() > 1) {
        const double factor = qSqrt(double(kMaxImageMemoryBytes) / double(normalized.sizeInBytes()));
        scaleDown(qBound(0.2, factor, 0.95));
    }

    return normalized;
}

void NoteEditor::showImagePreview(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    if (!previewDialog_.isNull()) {
        previewDialog_->close();
    }

    auto *dialog = new QDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(tr("Image preview %1 x %2").arg(image.width()).arg(image.height()));
    dialog->setModal(false);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setWidgetResizable(false);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scrollArea, 1);

    auto *imageLabel = new QLabel(scrollArea);
    imageLabel->setPixmap(QPixmap::fromImage(image));
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->resize(image.size());
    scrollArea->setWidget(imageLabel);

    auto *infoLabel =
        new QLabel(tr("Original size: %1 x %2").arg(image.width()).arg(image.height()), dialog);
    layout->addWidget(infoLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    layout->addWidget(buttons);

    QRect availableGeometry;
    if (window() != nullptr && window()->screen() != nullptr) {
        availableGeometry = window()->screen()->availableGeometry();
    } else if (QGuiApplication::primaryScreen() != nullptr) {
        availableGeometry = QGuiApplication::primaryScreen()->availableGeometry();
    }

    const QSize frameSize = image.size() + QSize(kPreviewMargin * 2, kPreviewMargin * 3);
    QSize dialogSize = frameSize;
    if (availableGeometry.isValid()) {
        dialogSize = dialogSize.boundedTo(
            QSize(int(availableGeometry.width() * kPreviewScreenRatio),
                  int(availableGeometry.height() * kPreviewScreenRatio)));
    }
    dialogSize = dialogSize.expandedTo(QSize(420, 320));
    dialog->resize(dialogSize);

    if (availableGeometry.isValid()) {
        dialog->move(availableGeometry.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    previewDialog_ = dialog;
}

QString NoteEditor::storeImage(const QImage &image, bool *scaled) const
{
    if (currentNoteId_ < 0 || image.isNull()) {
        return {};
    }

    if (scaled != nullptr) {
        *scaled = false;
    }

    const QString assetId =
        QStringLiteral("%1-%2")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces));

    if (encryptedAssetKey_.isEmpty()) {
        const QString noteAssetsPath = StoragePaths::noteAssetsPath(currentNoteId_);
        QDir directory;
        if (!directory.mkpath(noteAssetsPath)) {
            return {};
        }
        if (!writeImageAsPng(StoragePaths::notePlainAssetPath(currentNoteId_, assetId), image)) {
            return {};
        }
    }

    return noteAssetUrl(assetId).toString();
}
