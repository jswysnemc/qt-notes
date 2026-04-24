#include "ui/noteeditor.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>
#include <QtMath>

#include "common/storagepaths.h"

namespace {

const QString kRichContentPrefix = QStringLiteral("<!--qt-notes:rich-->\n");
constexpr qint64 kMaxSourceImageBytes = 20LL * 1024 * 1024;
constexpr qint64 kMaxImageMemoryBytes = 12LL * 1024 * 1024;
constexpr int kMaxStoredImageExtent = 2200;
constexpr int kPreviewMargin = 36;
constexpr double kPreviewScreenRatio = 0.9;

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

} // namespace

NoteEditor::NoteEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setPlaceholderText(QStringLiteral("开始记录..."));
    setAcceptRichText(false);
    setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTabStopDistance(28.0);
}

void NoteEditor::setCurrentNoteId(qint64 noteId)
{
    currentNoteId_ = noteId;
}

void NoteEditor::loadContent(const QString &content)
{
    clear();
    if (content.startsWith(kRichContentPrefix)) {
        setHtml(content.mid(kRichContentPrefix.size()));
        return;
    }

    setPlainText(content);
}

QString NoteEditor::serializedContent() const
{
    if (!documentContainsImages(document())) {
        return toPlainText();
    }

    return kRichContentPrefix + extractHtmlBody(toHtml());
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

void NoteEditor::contextMenuEvent(QContextMenuEvent *event)
{
    const QTextCursor imageCursor = imageCursorAt(event->pos());
    const bool hasImage = !imageCursor.isNull();
    const QPalette menuPalette = palette();
    const QString backgroundColor = menuPalette.color(QPalette::Base).name();
    const QString textColor = menuPalette.color(QPalette::Text).name();
    const QString borderColor = menuPalette.color(QPalette::Mid).name();
    const QString hoverColor = menuPalette.color(QPalette::Highlight).name();
    const QString hoverTextColor = menuPalette.color(QPalette::HighlightedText).name();
    const QString disabledTextColor =
        menuPalette.color(QPalette::Disabled, QPalette::Text).name();

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
    if (hasImage) {
        previewAction = menu.addAction(QStringLiteral("预览图片"));
        copyImageAction = menu.addAction(QStringLiteral("复制图片"));
        deleteImageAction = menu.addAction(QStringLiteral("删除图片"));
        menu.addSeparator();
    }

    QAction *undoAction = menu.addAction(QStringLiteral("撤销"));
    undoAction->setEnabled(document()->isUndoAvailable());

    QAction *redoAction = menu.addAction(QStringLiteral("重做"));
    redoAction->setEnabled(document()->isRedoAvailable());

    menu.addSeparator();

    QAction *cutAction = menu.addAction(QStringLiteral("剪切"));
    cutAction->setEnabled(textCursor().hasSelection() && isUndoRedoEnabled() && !isReadOnly());

    QAction *copyAction = menu.addAction(QStringLiteral("复制"));
    copyAction->setEnabled(textCursor().hasSelection());

    QAction *pasteAction = menu.addAction(QStringLiteral("粘贴"));
    pasteAction->setEnabled(canPaste() && !isReadOnly());

    QAction *deleteSelectionAction = menu.addAction(QStringLiteral("删除"));
    deleteSelectionAction->setEnabled(textCursor().hasSelection() && !isReadOnly());

    menu.addSeparator();

    QAction *selectAllAction = menu.addAction(QStringLiteral("全选"));
    selectAllAction->setEnabled(!document()->isEmpty());
    QAction *selected = menu.exec(event->globalPos());
    if (selected == nullptr) {
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

void NoteEditor::mouseReleaseEvent(QMouseEvent *event)
{
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

    const QString imageUrl = storeImage(image);
    if (imageUrl.isEmpty()) {
        return false;
    }

    const QImage storedImage = loadImage(imageUrl);
    const QSize displaySize = inlineImageSize(storedImage.isNull() ? image.size() : storedImage.size());

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
    const QTextCursor hitCursor = cursorForPosition(position);
    const int charCount = document()->characterCount();
    const int positions[2] = {hitCursor.position(), hitCursor.position() - 1};

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
    return readStoredImageFromFile(localFilePathFromImageName(imageUrl));
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
    dialog->setWindowTitle(QStringLiteral("图片预览 %1 x %2").arg(image.width()).arg(image.height()));
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
        new QLabel(QStringLiteral("原始大小：%1 x %2").arg(image.width()).arg(image.height()), dialog);
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

    const QString noteAssetsPath = StoragePaths::noteAssetsPath(currentNoteId_);
    QDir directory;
    if (!directory.mkpath(noteAssetsPath)) {
        return {};
    }

    const QImage storedImage = normalizedImage(image, scaled);

    const QString fileName =
        QStringLiteral("%1-%2.png")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString filePath = QDir(noteAssetsPath).filePath(fileName);
    if (!storedImage.save(filePath, "PNG")) {
        return {};
    }

    return QUrl::fromLocalFile(filePath).toString();
}
