#include "ui/noteeditor.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
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
#include <QScreen>
#include <QTimer>
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

constexpr auto kRichContentPrefix = "<!--qt-notes:rich-->\n";
constexpr qint64 kMaxSourceImageBytes = 20LL * 1024 * 1024;
constexpr qint64 kMaxImageMemoryBytes = 12LL * 1024 * 1024;
constexpr int kMaxStoredImageExtent = 2200;
constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 220;

class ImagePreviewPopup final : public QFrame
{
public:
    explicit ImagePreviewPopup(QWidget *parent = nullptr)
        : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_DeleteOnClose, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setObjectName(QStringLiteral("imagePreviewPopup"));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        imageLabel_ = new QLabel(this);
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setMinimumSize(140, 100);
        layout->addWidget(imageLabel_);

        infoLabel_ = new QLabel(this);
        infoLabel_->setAlignment(Qt::AlignCenter);
        layout->addWidget(infoLabel_);

        setStyleSheet(QStringLiteral(R"(
QFrame#imagePreviewPopup {
    background: rgba(28, 28, 28, 235);
    border: 1px solid rgba(255, 255, 255, 32);
    border-radius: 10px;
}
QLabel {
    color: #f4f4f4;
}
)"));
    }

    void setImage(const QImage &image)
    {
        imageLabel_->setPixmap(QPixmap::fromImage(
            image.scaled(kPreviewWidth,
                         kPreviewHeight,
                         Qt::KeepAspectRatio,
                         Qt::SmoothTransformation)));
        infoLabel_->setText(QStringLiteral("%1 x %2").arg(image.width()).arg(image.height()));
        adjustSize();
    }

private:
    QLabel *imageLabel_ = nullptr;
    QLabel *infoLabel_ = nullptr;
};

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
    if (content.startsWith(QLatin1StringView(kRichContentPrefix))) {
        setHtml(content.mid(qsizetype(sizeof(kRichContentPrefix) - 1)));
        return;
    }

    setPlainText(content);
}

QString NoteEditor::serializedContent() const
{
    if (!documentContainsImages(document())) {
        return toPlainText();
    }

    return QString::fromLatin1(kRichContentPrefix) + extractHtmlBody(toHtml());
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
    const QPoint globalPos = event->globalPos();
    const QTextCursor imageCursor = imageCursorAt(event->pos());
    const bool hasImage = !imageCursor.isNull();

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(R"(
QMenu {
    padding: 6px;
}
QMenu::item {
    padding: 6px 26px 6px 10px;
}
)"));

    if (hasImage) {
        QAction *previewAction = menu.addAction(QStringLiteral("预览图片"));
        QAction *copyImageAction = menu.addAction(QStringLiteral("复制图片"));
        QAction *deleteImageAction = menu.addAction(QStringLiteral("删除图片"));
        menu.addSeparator();

        connect(previewAction, &QAction::triggered, this, [this, imageCursor, globalPos]() {
            const QImage image = loadImage(imageCursor.charFormat().toImageFormat().name());
            if (!image.isNull()) {
                showImagePreview(image, globalPos);
            }
        });
        connect(copyImageAction, &QAction::triggered, this, [this, imageCursor]() {
            const QImage image = loadImage(imageCursor.charFormat().toImageFormat().name());
            if (!image.isNull()) {
                QApplication::clipboard()->setImage(image);
            }
        });
        connect(deleteImageAction, &QAction::triggered, this, [this, imageCursor]() {
            deleteImage(imageCursor);
        });
    }

    QAction *undoAction = menu.addAction(QStringLiteral("撤销"));
    undoAction->setEnabled(document()->isUndoAvailable());
    connect(undoAction, &QAction::triggered, this, &QTextEdit::undo);

    QAction *redoAction = menu.addAction(QStringLiteral("重做"));
    redoAction->setEnabled(document()->isRedoAvailable());
    connect(redoAction, &QAction::triggered, this, &QTextEdit::redo);

    menu.addSeparator();

    QAction *cutAction = menu.addAction(QStringLiteral("剪切"));
    cutAction->setEnabled(textCursor().hasSelection() && isUndoRedoEnabled() && !isReadOnly());
    connect(cutAction, &QAction::triggered, this, &QTextEdit::cut);

    QAction *copyAction = menu.addAction(QStringLiteral("复制"));
    copyAction->setEnabled(textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, this, &QTextEdit::copy);

    QAction *pasteAction = menu.addAction(QStringLiteral("粘贴"));
    pasteAction->setEnabled(canPaste() && !isReadOnly());
    connect(pasteAction, &QAction::triggered, this, &QTextEdit::paste);

    QAction *deleteSelectionAction = menu.addAction(QStringLiteral("删除"));
    deleteSelectionAction->setEnabled(textCursor().hasSelection() && !isReadOnly());
    connect(deleteSelectionAction, &QAction::triggered, this, [this]() {
        textCursor().removeSelectedText();
    });

    menu.addSeparator();

    QAction *selectAllAction = menu.addAction(QStringLiteral("全选"));
    selectAllAction->setEnabled(!document()->isEmpty());
    connect(selectAllAction, &QAction::triggered, this, &QTextEdit::selectAll);

    menu.exec(globalPos);
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
        showImagePreview(image, event->globalPosition().toPoint());
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

void NoteEditor::showImagePreview(const QImage &image, const QPoint &globalPos)
{
    if (image.isNull()) {
        return;
    }

    if (!previewPopup_.isNull()) {
        previewPopup_->close();
    }

    auto *popup = new ImagePreviewPopup();
    popup->setImage(image);

    QRect availableGeometry;
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        availableGeometry = screen->availableGeometry();
    } else if (QGuiApplication::primaryScreen() != nullptr) {
        availableGeometry = QGuiApplication::primaryScreen()->availableGeometry();
    }

    QPoint topLeft = globalPos + QPoint(16, 16);
    if (availableGeometry.isValid()) {
        if (topLeft.x() + popup->width() > availableGeometry.right()) {
            topLeft.setX(qMax(availableGeometry.left(),
                              availableGeometry.right() - popup->width() - 12));
        }
        if (topLeft.y() + popup->height() > availableGeometry.bottom()) {
            topLeft.setY(qMax(availableGeometry.top(),
                              availableGeometry.bottom() - popup->height() - 12));
        }
    }

    popup->move(topLeft);
    popup->show();
    previewPopup_ = popup;
    QTimer::singleShot(1500, popup, &QWidget::close);
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
