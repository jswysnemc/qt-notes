#include "ui/noteeditor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>

#include "common/storagepaths.h"

namespace {

constexpr auto kRichContentPrefix = "<!--qt-notes:rich-->\n";
constexpr int kMaxImageExtent = 2200;

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

        QImageReader reader(url.toLocalFile());
        reader.setAutoTransform(true);
        const QImage image = reader.read();
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

    QImage displayImage = image;
    if (displayImage.width() > kMaxImageExtent || displayImage.height() > kMaxImageExtent) {
        displayImage = displayImage.scaled(kMaxImageExtent,
                                           kMaxImageExtent,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    }

    QTextCursor cursor = textCursor();
    if (!cursor.atBlockStart() || !cursor.block().text().isEmpty()) {
        cursor.insertBlock();
    }

    QTextImageFormat format;
    format.setName(imageUrl);
    format.setWidth(displayImage.width());
    format.setHeight(displayImage.height());
    cursor.insertImage(format);
    cursor.insertBlock();
    setTextCursor(cursor);
    return true;
}

QString NoteEditor::storeImage(const QImage &image) const
{
    if (currentNoteId_ < 0 || image.isNull()) {
        return {};
    }

    const QString noteAssetsPath = StoragePaths::noteAssetsPath(currentNoteId_);
    QDir directory;
    if (!directory.mkpath(noteAssetsPath)) {
        return {};
    }

    QImage storedImage = image;
    if (storedImage.width() > kMaxImageExtent || storedImage.height() > kMaxImageExtent) {
        storedImage = storedImage.scaled(kMaxImageExtent,
                                         kMaxImageExtent,
                                         Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    }

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
