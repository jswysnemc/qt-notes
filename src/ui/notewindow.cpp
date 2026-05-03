#include "ui/notewindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QFontMetricsF>
#include <QScreen>
#include <QSet>
#include <QSignalBlocker>
#include <QSize>
#include <QShowEvent>
#include <QStyle>
#include <QTextCursor>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include "app/applicationcontroller.h"
#include "theme/themecatalog.h"
#include "ui/iconfactory.h"
#include "ui/noteeditor.h"
#include "ui/noteencryptiondialog.h"
#include "ui/notelistpopup.h"
#include "ui/settingsdialog.h"
#include "ui/titlebar.h"

namespace {

constexpr int kResizeBorder = 6;
constexpr int kDefaultWidth = 420;
constexpr int kDefaultHeight = 520;
constexpr int kMinFontSize = 10;
constexpr int kMaxFontSize = 40;
constexpr auto kGeometryFormat = "qt-notes-window-v1";

struct StoredWindowGeometry {
    QRect rect;
    QString screenName;
};

bool isWaylandPlatform()
{
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
}

QSize constrainedSize(const QSize &size)
{
    return QSize(qMax(size.width(), 280), qMax(size.height(), 220));
}

bool geometryLooksVisible(const QRect &geometry)
{
    if (geometry.width() < 160 || geometry.height() < 120) {
        return false;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->availableGeometry().intersects(geometry.adjusted(40, 40, -40, -40))) {
            return true;
        }
    }

    return false;
}

QScreen *screenByName(const QString &name)
{
    if (name.isEmpty()) {
        return nullptr;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->name() == name) {
            return screen;
        }
    }

    return nullptr;
}

QRect centeredFallbackGeometry(QScreen *preferredScreen = nullptr,
                               const QSize &preferredSize = QSize(kDefaultWidth, kDefaultHeight))
{
    QScreen *screen = preferredScreen != nullptr ? preferredScreen : QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return QRect(QPoint(0, 0), constrainedSize(preferredSize));
    }

    const QRect available = screen->availableGeometry();
    const QSize desiredSize = constrainedSize(preferredSize);
    const QSize targetSize(qMin(desiredSize.width(), available.width()),
                           qMin(desiredSize.height(), available.height()));
    const QPoint topLeft(available.center().x() - targetSize.width() / 2,
                         available.center().y() - targetSize.height() / 2);
    return QRect(topLeft, targetSize);
}

std::optional<StoredWindowGeometry> parseStoredGeometry(const QByteArray &payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1StringView(kGeometryFormat)) {
        return std::nullopt;
    }

    StoredWindowGeometry stored;
    stored.rect = QRect(root.value(QStringLiteral("x")).toInt(),
                        root.value(QStringLiteral("y")).toInt(),
                        root.value(QStringLiteral("width")).toInt(),
                        root.value(QStringLiteral("height")).toInt());
    stored.screenName = root.value(QStringLiteral("screen")).toString();
    return stored;
}

QByteArray serializeWindowGeometry(const QWidget *window)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kGeometryFormat));
    root.insert(QStringLiteral("platform"), QGuiApplication::platformName());
    root.insert(QStringLiteral("x"), window->x());
    root.insert(QStringLiteral("y"), window->y());
    root.insert(QStringLiteral("width"), window->width());
    root.insert(QStringLiteral("height"), window->height());
    root.insert(QStringLiteral("screen"),
                window->screen() != nullptr ? window->screen()->name() : QString());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString editorStylesheet(const ThemeSpec &theme)
{
    return QStringLiteral(R"(
QTextEdit#noteEditor {
    background: %1;
    color: %2;
    border: none;
    padding: 12px 14px 14px 14px;
    selection-background-color: %3;
    selection-color: %4;
}
QScrollBar:vertical,
QScrollBar:horizontal {
    background: transparent;
}
QScrollBar:vertical {
    width: 8px;
}
QScrollBar:horizontal {
    height: 8px;
}
QScrollBar::handle:vertical,
QScrollBar::handle:horizontal {
    border-radius: 4px;
    background: %5;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical,
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: transparent;
    border: none;
}
)")
        .arg(theme.editorColor.name(),
             theme.textColor.name(),
             theme.selectionColor.name(),
             theme.selectionTextColor.name(),
             theme.hoverColor.name());
}

QString lockedNotePlaceholderText(bool recoveryPasswordRequired)
{
    if (recoveryPasswordRequired) {
        return QStringLiteral(
            "该便签已锁定。\n\n连续输错 3 次后，必须输入复杂恢复密码才能重新解锁。\n标题只保留部分字符供辨认，打开设置里的解锁按钮继续。");
    }

    return QStringLiteral(
        "该便签已加密。\n\n打开设置里的解锁按钮，输入简单密码后可临时解锁。\n解锁成功前只显示部分标题，不显示正文。");
}

} // namespace

NoteWindow::NoteWindow(ApplicationController *controller, const NoteData &note, QWidget *parent)
    : QWidget(parent)
    , controller_(controller)
    , note_(note)
    , theme_(ThemeCatalog::themeById(note.themeId))
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::Window, true);
    setMinimumSize(280, 220);
    setObjectName(QStringLiteral("noteWindow"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    windowSurface_ = new QFrame(this);
    windowSurface_->setObjectName(QStringLiteral("windowSurface"));
    rootLayout->addWidget(windowSurface_);

    auto *surfaceLayout = new QVBoxLayout(windowSurface_);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    titleBar_ = new TitleBar(windowSurface_);
    surfaceLayout->addWidget(titleBar_);

    editor_ = new NoteEditor(windowSurface_);
    editor_->setObjectName(QStringLiteral("noteEditor"));
    surfaceLayout->addWidget(editor_, 1);

    noteListPopup_ = new NoteListPopup(this);

    contentSaveTimer_ = new QTimer(this);
    contentSaveTimer_->setSingleShot(true);
    contentSaveTimer_->setInterval(400);

    appearanceSaveTimer_ = new QTimer(this);
    appearanceSaveTimer_->setSingleShot(true);
    appearanceSaveTimer_->setInterval(200);

    geometrySaveTimer_ = new QTimer(this);
    geometrySaveTimer_->setSingleShot(true);
    geometrySaveTimer_->setInterval(450);

    {
        const QSignalBlocker blocker(editor_);
        editor_->setCurrentNoteId(note_.id);
        editor_->setEncryptedAssetKey(QByteArray());
        editor_->loadContent(note_.isEncrypted ? QString() : note_.content);
    }

    titleBar_->setTitle(note_.title);
    applyEditorSettings();
    applyTheme();
    applySecurityState(true);
    restoreWindowGeometry();
    installResizeEventFilters();

    connect(titleBar_, &TitleBar::listRequested, this, &NoteWindow::showNoteList);
    connect(titleBar_, &TitleBar::themeRequested, this, &NoteWindow::showThemeMenu);
    connect(titleBar_, &TitleBar::settingsRequested, this, &NoteWindow::showSettingsDialog);
    connect(titleBar_, &TitleBar::securityRequested, this, &NoteWindow::handleSecurityAction);
    connect(titleBar_, &TitleBar::newNoteRequested, controller_, &ApplicationController::createAndOpenNote);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);
    connect(titleBar_, &TitleBar::titleEdited, this, &NoteWindow::updateTitle);
    connect(titleBar_, &TitleBar::dragRequested, this, &NoteWindow::startWindowMove);

    connect(editor_, &QTextEdit::textChanged, this, [this]() {
        contentDirty_ = true;
        contentSaveTimer_->start();
    });
    connect(editor_, &NoteEditor::fontZoomRequested, this, &NoteWindow::adjustFontSize);
    connect(contentSaveTimer_, &QTimer::timeout, this, &NoteWindow::flushContent);
    connect(appearanceSaveTimer_, &QTimer::timeout, this, &NoteWindow::flushAppearance);
    connect(geometrySaveTimer_, &QTimer::timeout, this, &NoteWindow::flushGeometry);
    connect(noteListPopup_, &NoteListPopup::noteChosenInCurrentWindow, this, &NoteWindow::openNoteInCurrentWindow);
    connect(noteListPopup_, &NoteListPopup::noteChosenInNewWindow, controller_, &ApplicationController::openNote);
    connect(noteListPopup_, &NoteListPopup::notesChosen, this, &NoteWindow::openSelectedNotes);
    connect(noteListPopup_, &NoteListPopup::renameRequested, this, &NoteWindow::renameNoteFromList);
    connect(noteListPopup_, &NoteListPopup::deleteRequested, this, &NoteWindow::deleteNotesFromList);
    connect(controller_,
            &ApplicationController::globalFontSettingsChanged,
            this,
            [this](const QString &, int) {
                applyEditorSettings();
            });
    connect(controller_,
            &ApplicationController::noteTitleChanged,
            this,
            [this](qint64 id, const QString &title) {
                if (id != note_.id) {
                    return;
                }
                if (note_.isEncrypted && !encryptedNoteUnlocked()) {
                    return;
                }
                note_.title = title;
                titleBar_->setTitle(note_.title);
            });
    connect(controller_, &ApplicationController::notesChanged, this, [this]() {
        if (noteListPopup_->isVisible()) {
            noteListPopup_->setNotes(controller_->noteSummaries());
        }
    });
}

bool NoteWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget == nullptr) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        const QPoint windowPosition = mapChildPositionToWindow(widget, mouseEvent->position());
        const Qt::Edges edges = resizeEdgesAt(windowPosition);
        if (edges != Qt::Edges() && windowHandle() != nullptr) {
            updateResizeCursor(windowPosition, widget);
            windowHandle()->startSystemResize(edges);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        updateResizeCursor(mapChildPositionToWindow(widget, mouseEvent->position()), widget);
        break;
    }
    case QEvent::Leave:
        widget->unsetCursor();
        unsetCursor();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

qint64 NoteWindow::noteId() const
{
    return note_.id;
}

void NoteWindow::switchToNote(const NoteData &note)
{
    noteListPopup_->hide();
    contentSaveTimer_->stop();
    appearanceSaveTimer_->stop();
    geometrySaveTimer_->stop();
    clearUnlockedDataKey();

    note_ = note;
    theme_ = ThemeCatalog::themeById(note_.themeId);

    {
        const QSignalBlocker blocker(editor_);
        editor_->setCurrentNoteId(note_.id);
        editor_->setEncryptedAssetKey(QByteArray());
        editor_->loadContent(note_.isEncrypted ? QString() : note_.content);
        QTextCursor cursor = editor_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor_->setTextCursor(cursor);
    }

    contentDirty_ = false;
    appearanceDirty_ = false;
    geometryDirty_ = false;
    deleting_ = false;

    titleBar_->setTitle(note_.title);
    applyTheme();
    applyEditorSettings();
    applySecurityState(true);
}

void NoteWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const Qt::Edges edges = resizeEdgesAt(event->pos());
        if (edges != Qt::Edges() && windowHandle() != nullptr) {
            windowHandle()->startSystemResize(edges);
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void NoteWindow::mouseMoveEvent(QMouseEvent *event)
{
    updateResizeCursor(event->pos());
    QWidget::mouseMoveEvent(event);
}

void NoteWindow::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}

void NoteWindow::moveEvent(QMoveEvent *event)
{
    geometryDirty_ = true;
    geometrySaveTimer_->start();
    QWidget::moveEvent(event);
}

void NoteWindow::resizeEvent(QResizeEvent *event)
{
    geometryDirty_ = true;
    geometrySaveTimer_->start();
    QWidget::resizeEvent(event);
}

void NoteWindow::closeEvent(QCloseEvent *event)
{
    if (!deleting_) {
        flushContent();
        flushAppearance();
        flushGeometry();
        controller_->rememberClosedNote(note_.id);
    } else {
        contentSaveTimer_->stop();
        appearanceSaveTimer_->stop();
        geometrySaveTimer_->stop();
    }
    clearUnlockedDataKey();
    QWidget::closeEvent(event);
}

void NoteWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (initialRestoreApplied_) {
        return;
    }

    initialRestoreApplied_ = true;
    QTimer::singleShot(0, this, [this]() {
        applyPendingGeometry();
    });

    if (isWaylandPlatform()) {
        QTimer::singleShot(120, this, [this]() {
            applyPendingGeometry();
        });
    }
}

void NoteWindow::applyTheme()
{
    theme_ = ThemeCatalog::themeById(note_.themeId);
    titleBar_->setTheme(theme_);
    noteListPopup_->setTheme(theme_);

    setStyleSheet(QStringLiteral(R"(
QWidget#noteWindow {
    background: transparent;
}
QFrame#windowSurface {
    background: %1;
    border: none;
}
)")
                      .arg(theme_.surfaceColor.name()));

    editor_->setStyleSheet(editorStylesheet(theme_));
    QPalette palette = editor_->palette();
    palette.setColor(QPalette::Base, theme_.editorColor);
    palette.setColor(QPalette::Text, theme_.textColor);
    palette.setColor(QPalette::Highlight, theme_.selectionColor);
    palette.setColor(QPalette::HighlightedText, theme_.selectionTextColor);
    editor_->setPalette(palette);
}

void NoteWindow::applyEditorSettings()
{
    QFont font = QApplication::font();
    const QString globalFontFamily = controller_->globalFontFamily();
    if (!globalFontFamily.isEmpty()) {
        font.setFamily(globalFontFamily);
    }
    font.setPointSize(qBound(kMinFontSize, controller_->globalFontPointSize(), kMaxFontSize));

    editor_->document()->setDefaultFont(font);
    editor_->setFont(font);
    editor_->viewport()->setFont(font);
    editor_->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    editor_->update();

    if (note_.wrapMode) {
        editor_->setLineWrapMode(QTextEdit::WidgetWidth);
        editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        editor_->setLineWrapMode(QTextEdit::NoWrap);
        editor_->setWordWrapMode(QTextOption::NoWrap);
        editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void NoteWindow::applySecurityState(bool promptForUnlock)
{
    if (!note_.isEncrypted) {
        titleBar_->setSecurityState(false, false);
        titleBar_->setTitleEditable(true);
        editor_->setReadOnly(false);
        editor_->setImageAttachmentsEnabled(true);
        editor_->setEncryptedAssetKey(QByteArray());
        return;
    }

    const bool unlocked = encryptedNoteUnlocked();
    titleBar_->setSecurityState(true, unlocked);
    titleBar_->setTitleEditable(unlocked);
    editor_->setImageAttachmentsEnabled(unlocked);

    if (unlocked) {
        editor_->setReadOnly(false);
        editor_->setEncryptedAssetKey(unlockedDataKey_);
        titleBar_->setTitle(note_.title);
        return;
    }

    editor_->setEncryptedAssetKey(QByteArray());
    const QString lockedTitle = note_.title.trimmed().isEmpty() ? maskedEncryptedTitle(QString())
                                                                : note_.title;
    titleBar_->setTitle(lockedTitle);
    {
        const QSignalBlocker blocker(editor_);
        editor_->setReadOnly(false);
        editor_->loadContent(lockedNotePlaceholderText(note_.recoveryPasswordRequired));
        editor_->setReadOnly(true);
        QTextCursor cursor = editor_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor_->setTextCursor(cursor);
    }
    note_.content.clear();

    if (promptForUnlock) {
        QTimer::singleShot(0, this, &NoteWindow::handleSecurityAction);
    }
}

bool NoteWindow::saveEncryptedSnapshot(QString *errorMessage)
{
    if (!note_.isEncrypted || !encryptedNoteUnlocked()) {
        return false;
    }

    QString currentContent;
    if (!editor_->persistImageAttachments(true, &currentContent, errorMessage)) {
        return false;
    }
    if (!controller_->saveEncryptedNote(note_.id,
                                        note_.title,
                                        currentContent,
                                        unlockedDataKey_,
                                        errorMessage)) {
        return false;
    }

    note_.content = currentContent;
    contentDirty_ = false;
    return true;
}

void NoteWindow::clearUnlockedDataKey()
{
    editor_->setEncryptedAssetKey(QByteArray());
    NoteCrypto::wipe(&unlockedDataKey_);
}

bool NoteWindow::encryptedNoteUnlocked() const
{
    return note_.isEncrypted && !unlockedDataKey_.isEmpty();
}

void NoteWindow::restoreWindowGeometry()
{
    if (!note_.windowGeometry.isEmpty()) {
        if (const std::optional<StoredWindowGeometry> stored =
                parseStoredGeometry(note_.windowGeometry);
            stored.has_value()) {
            QScreen *storedScreen = screenByName(stored->screenName);
            if (isWaylandPlatform()) {
                pendingRestoreGeometry_ = centeredFallbackGeometry(storedScreen, stored->rect.size());
                pendingRestoreMove_ = false;
                return;
            }

            QRect target = QRect(stored->rect.topLeft(), constrainedSize(stored->rect.size()));
            if (!geometryLooksVisible(target)) {
                target = centeredFallbackGeometry(storedScreen, target.size());
            }
            pendingRestoreGeometry_ = target;
            pendingRestoreMove_ = true;
            return;
        }

        const bool restored = restoreGeometry(note_.windowGeometry);
        if (restored && (!isWaylandPlatform() || geometryLooksVisible(frameGeometry()))) {
            pendingRestoreGeometry_ = QRect(pos(), size());
            pendingRestoreMove_ = !isWaylandPlatform();
            return;
        }
    }

    pendingRestoreGeometry_ = centeredFallbackGeometry();
    pendingRestoreMove_ = !isWaylandPlatform();
}

void NoteWindow::applyPendingGeometry()
{
    if (!pendingRestoreGeometry_.isValid()) {
        return;
    }

    resize(pendingRestoreGeometry_.size());
    if (pendingRestoreMove_) {
        move(pendingRestoreGeometry_.topLeft());
    }
}

void NoteWindow::showThemeMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(R"(
QMenu {
    background: %1;
    color: %2;
    border: 1px solid %3;
    padding: 6px;
}
QMenu::item {
    padding: 7px 24px 7px 10px;
    border-radius: 8px;
}
QMenu::item:selected {
    background: %4;
}
)")
                           .arg(theme_.surfaceColor.name(),
                                theme_.textColor.name(),
                                theme_.borderColor.name(),
                                theme_.hoverColor.name()));

    for (const ThemeSpec &theme : ThemeCatalog::themes()) {
        QAction *action = menu.addAction(
            IconFactory::swatchIcon(theme.titleBarColor, theme.borderColor), theme.name);
        action->setData(theme.id);
        if (theme.id == note_.themeId) {
            action->setCheckable(true);
            action->setChecked(true);
        }
    }

    const QPoint popupPosition =
        titleBar_->themeButtonWidget()->mapToGlobal(QPoint(0, titleBar_->height() + 6));
    QAction *selected = menu.exec(popupPosition);
    if (selected == nullptr) {
        return;
    }

    const QString nextThemeId = selected->data().toString();
    if (nextThemeId == note_.themeId) {
        return;
    }

    note_.themeId = nextThemeId;
    applyTheme();
    appearanceDirty_ = true;
    appearanceSaveTimer_->start();
}

void NoteWindow::handleSecurityAction()
{
    if (!note_.isEncrypted) {
        if (!controller_->hasCachedEncryptionPasswords()) {
            const bool needsSetup = !controller_->hasEncryptionPasswordsConfigured();
            EncryptNoteDialog dialog(note_.title,
                                     needsSetup ? EncryptNoteDialogMode::SetupGlobalPasswords
                                                : EncryptNoteDialogMode::EnterGlobalPasswords,
                                     theme_,
                                     this);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }

            QString errorMessage;
            const bool passwordsReady =
                needsSetup
                    ? controller_->setupGlobalEncryptionPasswords(dialog.simplePassword(),
                                                                  dialog.recoveryPassword(),
                                                                  &errorMessage)
                    : controller_->unlockGlobalEncryptionPasswords(dialog.simplePassword(),
                                                                   dialog.recoveryPassword(),
                                                                   &errorMessage);
            if (!passwordsReady) {
                QMessageBox::warning(this,
                                     needsSetup ? QStringLiteral("设置全局密码失败")
                                                : QStringLiteral("全局密码错误"),
                                     errorMessage.isEmpty()
                                         ? QStringLiteral("当前无法使用这套全局加密密码。")
                                         : errorMessage);
                return;
            }
        }

        const QString currentContent = editor_->serializedContent();
        NoteEncryptionResult result =
            controller_->enableNoteEncryption(note_.id, note_.title, currentContent);
        if (!result.success) {
            QMessageBox::warning(this,
                                 QStringLiteral("启用加密失败"),
                                 result.errorMessage.isEmpty()
                                     ? QStringLiteral("当前便签无法启用加密。")
                                     : result.errorMessage);
            return;
        }

        note_ = result.note;
        clearUnlockedDataKey();
        unlockedDataKey_ = result.dataKey;
        editor_->setEncryptedAssetKey(unlockedDataKey_);
        QString encryptedContent;
        QString errorMessage;
        if (!editor_->persistImageAttachments(true, &encryptedContent, &errorMessage)
            || !controller_->saveEncryptedNote(note_.id,
                                               note_.title,
                                               encryptedContent,
                                               unlockedDataKey_,
                                               &errorMessage)) {
            applySecurityState(false);
            QMessageBox::warning(this,
                                 QStringLiteral("启用加密失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("当前便签图片附件无法完成加密。")
                                     : errorMessage);
            return;
        }

        note_.content = encryptedContent;
        {
            const QSignalBlocker blocker(editor_);
            editor_->loadContent(note_.content);
            QTextCursor cursor = editor_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            editor_->setTextCursor(cursor);
        }
        contentDirty_ = false;
        titleBar_->setTitle(note_.title);
        applySecurityState(false);
        return;
    }

    if (encryptedNoteUnlocked()) {
        QString errorMessage;
        if (!saveEncryptedSnapshot(&errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("锁定失败"),
                                 errorMessage.isEmpty() ? QStringLiteral("当前便签无法完成锁定。")
                                                        : errorMessage);
            return;
        }

        clearUnlockedDataKey();
        note_.title = maskedEncryptedTitle(note_.title);
        note_.failedUnlockAttempts = 0;
        note_.recoveryPasswordRequired = false;
        applySecurityState(false);
        return;
    }

    const bool useRecoveryPassword = note_.recoveryPasswordRequired;
    const int remainingSimpleAttempts =
        qMax(0, NoteCrypto::kFailedAttemptsBeforeRecovery - note_.failedUnlockAttempts);
    UnlockNoteDialog dialog(useRecoveryPassword, remainingSimpleAttempts, theme_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    NoteUnlockAttemptResult result =
        useRecoveryPassword
            ? controller_->unlockNoteWithRecoveryPassword(note_.id, dialog.password())
            : controller_->unlockNoteWithSimplePassword(note_.id, dialog.password());

    switch (result.status) {
    case NoteCrypto::UnlockStatus::Success: {
        note_ = result.note;
        clearUnlockedDataKey();
        unlockedDataKey_ = result.dataKey;
        {
            const QSignalBlocker blocker(editor_);
            editor_->setReadOnly(false);
            editor_->setEncryptedAssetKey(unlockedDataKey_);
            editor_->loadContent(note_.content);
            QTextCursor cursor = editor_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            editor_->setTextCursor(cursor);
        }
        contentDirty_ = false;
        applySecurityState(false);
        return;
    }
    case NoteCrypto::UnlockStatus::WrongPassword:
        note_.failedUnlockAttempts = result.failedUnlockAttempts;
        note_.recoveryPasswordRequired = result.note.recoveryPasswordRequired;
        applySecurityState(false);
        QMessageBox::warning(this,
                             QStringLiteral("密码错误"),
                             useRecoveryPassword
                                 ? QStringLiteral("复杂恢复密码错误。")
                                 : QStringLiteral("简单密码错误，还剩 %1 次机会。")
                                       .arg(result.remainingSimpleAttempts));
        return;
    case NoteCrypto::UnlockStatus::RecoveryPasswordRequired:
        note_.failedUnlockAttempts = result.failedUnlockAttempts > 0
                                         ? result.failedUnlockAttempts
                                         : NoteCrypto::kFailedAttemptsBeforeRecovery;
        note_.recoveryPasswordRequired = true;
        applySecurityState(false);
        QMessageBox::warning(this,
                             QStringLiteral("便签已锁定"),
                             QStringLiteral("简单密码已连续输错 3 次，必须输入复杂恢复密码。"));
        QTimer::singleShot(0, this, &NoteWindow::handleSecurityAction);
        return;
    case NoteCrypto::UnlockStatus::NotEncrypted:
        note_ = result.note;
        clearUnlockedDataKey();
        applySecurityState(false);
        return;
    case NoteCrypto::UnlockStatus::InvalidData:
    default:
        QMessageBox::warning(this,
                             QStringLiteral("解锁失败"),
                             result.errorMessage.isEmpty() ? QStringLiteral("便签密文无法解开。")
                                                           : result.errorMessage);
        return;
    }
}

void NoteWindow::showSettingsDialog()
{
    SettingsDialog dialog(note_.title,
                          note_.wrapMode,
                          controller_->globalFontFamily(),
                          controller_->globalFontPointSize(),
                          controller_->recentFonts(),
                          controller_->sortMode(),
                          controller_->startupNoteMode(),
                          note_.isEncrypted,
                          encryptedNoteUnlocked(),
                          controller_->hasEncryptionPasswordsConfigured(),
                          theme_,
                          this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (dialog.securityActionRequested()) {
        handleSecurityAction();
        return;
    }

    if (dialog.changeSimplePasswordRequested()) {
        ChangeEncryptionPasswordDialog passwordDialog(ChangeEncryptionPasswordMode::SimplePassword,
                                                      theme_,
                                                      this);
        if (passwordDialog.exec() != QDialog::Accepted) {
            return;
        }

        QString errorMessage;
        if (!controller_->changeSimpleEncryptionPassword(
                passwordDialog.currentSimplePassword(),
                passwordDialog.currentRecoveryPassword(),
                passwordDialog.newPassword(),
                &errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("修改短密码失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("当前无法修改短密码。")
                                     : errorMessage);
            return;
        }

        QMessageBox::information(this,
                                 QStringLiteral("修改完成"),
                                 QStringLiteral("短密码已更新。"));
        return;
    }

    if (dialog.changeRecoveryPasswordRequested()) {
        ChangeEncryptionPasswordDialog passwordDialog(ChangeEncryptionPasswordMode::RecoveryPassword,
                                                      theme_,
                                                      this);
        if (passwordDialog.exec() != QDialog::Accepted) {
            return;
        }

        QString errorMessage;
        if (!controller_->changeRecoveryEncryptionPassword(
                passwordDialog.currentSimplePassword(),
                passwordDialog.currentRecoveryPassword(),
                passwordDialog.newPassword(),
                &errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("修改长密码失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("当前无法修改长密码。")
                                     : errorMessage);
            return;
        }

        QMessageBox::information(this,
                                 QStringLiteral("修改完成"),
                                 QStringLiteral("长密码已更新。"));
        return;
    }

    if (dialog.disableEncryptionRequested()) {
        QString currentContent;
        QString errorMessage;
        if (!editor_->persistImageAttachments(false, &currentContent, &errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("取消加密失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("当前便签图片附件无法转回普通存储。")
                                     : errorMessage);
            return;
        }
        if (!controller_->disableNoteEncryption(note_.id,
                                                note_.title,
                                                currentContent,
                                                &errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("取消加密失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("当前便签无法转回普通便签。")
                                     : errorMessage);
            return;
        }

        clearUnlockedDataKey();
        note_.isEncrypted = false;
        note_.content = currentContent;
        note_.failedUnlockAttempts = 0;
        note_.recoveryPasswordRequired = false;
        contentDirty_ = false;
        titleBar_->setTitle(note_.title);
        applySecurityState(false);
    }

    if (dialog.deleteRequested()) {
        deleteCurrentNote();
        return;
    }

    note_.wrapMode = dialog.wrapMode();
    controller_->setGlobalFontSettings(dialog.fontFamily(), dialog.fontPointSize());
    applyEditorSettings();

    controller_->setSortMode(dialog.sortMode());
    controller_->setStartupNoteMode(dialog.startupNoteMode());
    appearanceDirty_ = true;
    appearanceSaveTimer_->start();
}

void NoteWindow::showNoteList()
{
    noteListPopup_->setNotes(controller_->noteSummaries());
    noteListPopup_->popupBelow(titleBar_->listButtonWidget());
}

void NoteWindow::openNoteInCurrentWindow(qint64 id)
{
    if (id < 0) {
        return;
    }

    contentSaveTimer_->stop();
    appearanceSaveTimer_->stop();
    geometrySaveTimer_->stop();
    flushContent();
    flushAppearance();
    flushGeometry();

    controller_->switchWindowToNote(this, id);
}

void NoteWindow::openSelectedNotes(const QVector<qint64> &ids)
{
    for (qint64 id : ids) {
        controller_->openNote(id);
    }
}

void NoteWindow::renameNoteFromList(qint64 id, const QString &title)
{
    const QString nextTitle = title.trimmed();
    if (id < 0 || nextTitle.isEmpty()) {
        return;
    }

    controller_->saveTitle(id, nextTitle);
}

void NoteWindow::deleteNotesFromList(const QVector<qint64> &ids)
{
    QVector<qint64> normalizedIds;
    normalizedIds.reserve(ids.size());

    QSet<qint64> seenIds;
    for (qint64 id : ids) {
        if (id < 0 || seenIds.contains(id)) {
            continue;
        }
        seenIds.insert(id);
        normalizedIds.push_back(id);
    }

    if (normalizedIds.isEmpty()) {
        return;
    }

    QString message;
    if (normalizedIds.size() == 1) {
        const std::optional<NoteData> targetNote = controller_->note(normalizedIds.front());
        const QString displayTitle =
            targetNote.has_value() && !targetNote->title.trimmed().isEmpty()
                ? targetNote->title
                : QStringLiteral("当前便签");
        message = QStringLiteral("删除后无法恢复：\n%1").arg(displayTitle);
    } else {
        message = QStringLiteral("删除后无法恢复：\n已选 %1 个便签").arg(normalizedIds.size());
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(this,
                                                                    QStringLiteral("确认删除"),
                                                                    message,
                                                                    QMessageBox::Yes
                                                                        | QMessageBox::Cancel,
                                                                    QMessageBox::Cancel);
    if (result != QMessageBox::Yes) {
        return;
    }

    if (!controller_->deleteNotes(normalizedIds)) {
        QMessageBox::warning(this,
                             QStringLiteral("删除失败"),
                             QStringLiteral("选中的便签没有全部删除成功，请稍后重试。"));
    }
}

void NoteWindow::deleteCurrentNote()
{
    if (!controller_->deleteNote(note_.id)) {
        QMessageBox::warning(this,
                             QStringLiteral("删除失败"),
                             QStringLiteral("当前便签删除失败，请稍后重试。"));
    }
}

void NoteWindow::updateTitle(const QString &title)
{
    if (note_.isEncrypted && !encryptedNoteUnlocked()) {
        titleBar_->setTitle(note_.title.trimmed().isEmpty() ? maskedEncryptedTitle(QString())
                                                            : note_.title);
        return;
    }

    QString nextTitle = title.trimmed();
    if (nextTitle.isEmpty()) {
        nextTitle = note_.title.isEmpty() ? timestampTitle(note_.createdAt) : note_.title;
    }

    if (nextTitle == note_.title) {
        titleBar_->setTitle(nextTitle);
        return;
    }

    note_.title = nextTitle;
    titleBar_->setTitle(note_.title);

    if (note_.isEncrypted) {
        QString errorMessage;
        if (!saveEncryptedSnapshot(&errorMessage)) {
            QMessageBox::warning(this,
                                 QStringLiteral("保存失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("加密便签标题保存失败。")
                                     : errorMessage);
        }
        return;
    }

    controller_->saveTitle(note_.id, note_.title);
}

void NoteWindow::adjustFontSize(int steps)
{
    const QString currentFamily = controller_->globalFontFamily().isEmpty()
                                      ? editor_->font().family()
                                      : controller_->globalFontFamily();
    const int nextSize =
        qBound(kMinFontSize, controller_->globalFontPointSize() + steps, kMaxFontSize);
    controller_->setGlobalFontSettings(currentFamily, nextSize);
}

void NoteWindow::startWindowMove()
{
    if (windowHandle() != nullptr) {
        windowHandle()->startSystemMove();
    }
}

void NoteWindow::prepareForDeletion()
{
    deleting_ = true;
    noteListPopup_->hide();
    contentSaveTimer_->stop();
    appearanceSaveTimer_->stop();
    geometrySaveTimer_->stop();
    clearUnlockedDataKey();
}

void NoteWindow::flushContent()
{
    if (!contentDirty_) {
        return;
    }

    if (note_.isEncrypted && !encryptedNoteUnlocked()) {
        contentDirty_ = false;
        return;
    }

    QString text;
    QString errorMessage;
    if (!editor_->persistImageAttachments(note_.isEncrypted, &text, &errorMessage)) {
        contentDirty_ = true;
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("图片附件保存失败。")
                                                    : errorMessage);
        return;
    }

    if (note_.isEncrypted) {
        if (text == note_.content) {
            contentDirty_ = false;
            return;
        }

        note_.content = text;
        if (!controller_->saveEncryptedNote(note_.id,
                                            note_.title,
                                            note_.content,
                                            unlockedDataKey_,
                                            &errorMessage)) {
            contentDirty_ = true;
            QMessageBox::warning(this,
                                 QStringLiteral("保存失败"),
                                 errorMessage.isEmpty()
                                     ? QStringLiteral("加密便签内容保存失败。")
                                     : errorMessage);
            return;
        }

        contentDirty_ = false;
        return;
    }

    contentDirty_ = false;
    if (text == note_.content) {
        return;
    }

    note_.content = text;
    controller_->saveContent(note_.id, note_.content);
}

void NoteWindow::flushAppearance()
{
    if (!appearanceDirty_) {
        return;
    }

    appearanceDirty_ = false;
    controller_->saveAppearance(note_.id, note_.themeId, note_.wrapMode);
}

void NoteWindow::flushGeometry()
{
    if (!geometryDirty_) {
        return;
    }

    geometryDirty_ = false;
    const QByteArray geometry = serializeWindowGeometry(this);
    if (geometry == note_.windowGeometry) {
        return;
    }

    note_.windowGeometry = geometry;
    controller_->saveGeometry(note_.id, geometry);
}

void NoteWindow::installResizeEventFilters()
{
    QList<QWidget *> targets = windowSurface_->findChildren<QWidget *>();
    targets.prepend(windowSurface_);

    for (QWidget *target : std::as_const(targets)) {
        if (target == nullptr) {
            continue;
        }
        target->installEventFilter(this);
        target->setMouseTracking(true);
    }
}

QPoint NoteWindow::mapChildPositionToWindow(QWidget *child, const QPointF &position) const
{
    if (child == nullptr) {
        return position.toPoint();
    }

    return mapFromGlobal(child->mapToGlobal(position.toPoint()));
}

void NoteWindow::updateResizeCursor(const QPoint &position, QWidget *cursorTarget)
{
    QWidget *target = cursorTarget != nullptr ? cursorTarget : this;

    switch (resizeEdgesAt(position)) {
    case Qt::LeftEdge:
    case Qt::RightEdge:
        setCursor(Qt::SizeHorCursor);
        target->setCursor(Qt::SizeHorCursor);
        break;
    case Qt::TopEdge:
    case Qt::BottomEdge:
        setCursor(Qt::SizeVerCursor);
        target->setCursor(Qt::SizeVerCursor);
        break;
    case Qt::TopEdge | Qt::LeftEdge:
    case Qt::BottomEdge | Qt::RightEdge:
        setCursor(Qt::SizeFDiagCursor);
        target->setCursor(Qt::SizeFDiagCursor);
        break;
    case Qt::TopEdge | Qt::RightEdge:
    case Qt::BottomEdge | Qt::LeftEdge:
        setCursor(Qt::SizeBDiagCursor);
        target->setCursor(Qt::SizeBDiagCursor);
        break;
    default:
        target->unsetCursor();
        unsetCursor();
        break;
    }
}

Qt::Edges NoteWindow::resizeEdgesAt(const QPoint &position) const
{
    Qt::Edges edges;
    if (position.x() <= kResizeBorder) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= width() - kResizeBorder) {
        edges |= Qt::RightEdge;
    }

    if (position.y() <= kResizeBorder) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= height() - kResizeBorder) {
        edges |= Qt::BottomEdge;
    }

    return edges;
}
