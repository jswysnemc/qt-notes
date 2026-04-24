#include "app/applicationcontroller.h"

#include <QDir>
#include <QSet>
#include <QTimer>

#include "common/storagepaths.h"
#include "ui/notewindow.h"

namespace {

constexpr auto kSortModeKey = "ui/sort_mode";
constexpr auto kStartupModeKey = "ui/startup_note_mode";
constexpr auto kLastClosedNoteIdKey = "ui/last_closed_note_id";
constexpr auto kRecentFontsKey = "ui/recent_fonts";
constexpr auto kGlobalFontFamilyKey = "ui/global_font_family";
constexpr auto kGlobalFontPointSizeKey = "ui/global_font_point_size";
constexpr int kMaxRecentFonts = 8;
constexpr int kDefaultFontPointSize = 14;

void removeNoteAssets(qint64 id)
{
    if (id < 0) {
        return;
    }

    QDir noteAssets(StoragePaths::noteAssetsPath(id));
    if (!noteAssets.exists()) {
        return;
    }

    if (!noteAssets.removeRecursively()) {
        qWarning() << "ApplicationController failed to remove note assets:" << id << noteAssets.path();
    }
}

} // namespace

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , settings_(QSettings::IniFormat,
                QSettings::UserScope,
                QStringLiteral("snemc"),
                QStringLiteral("qt-notes"))
{
}

bool ApplicationController::initialize(QString *errorMessage)
{
    return repository_.initialize(errorMessage);
}

void ApplicationController::start()
{
    qint64 startupId = this->startupNoteId();
    if (startupId < 0) {
        createAndOpenNote();
        return;
    }

    openNote(startupId);
}

void ApplicationController::createAndOpenNote()
{
    const NoteData noteData = repository_.createNote();
    if (noteData.id < 0) {
        return;
    }

    emit notesChanged();
    openWindowFor(noteData);
}

void ApplicationController::openNote(qint64 id)
{
    if (windows_.contains(id) && !windows_.value(id).isNull()) {
        NoteWindow *window = windows_.value(id);
        window->showNormal();
        window->raise();
        window->activateWindow();
        return;
    }

    const std::optional<NoteData> noteData = repository_.noteById(id);
    if (!noteData.has_value()) {
        return;
    }

    openWindowFor(noteData.value());
}

bool ApplicationController::deleteNote(qint64 id)
{
    return deleteNotes({id});
}

bool ApplicationController::switchWindowToNote(NoteWindow *window, qint64 id)
{
    if (window == nullptr || id < 0) {
        return false;
    }

    if (window->noteId() == id) {
        window->showNormal();
        window->raise();
        window->activateWindow();
        return true;
    }

    if (windows_.contains(id) && !windows_.value(id).isNull() && windows_.value(id) != window) {
        NoteWindow *existingWindow = windows_.value(id);
        existingWindow->showNormal();
        existingWindow->raise();
        existingWindow->activateWindow();
        return false;
    }

    const std::optional<NoteData> noteData = repository_.noteById(id);
    if (!noteData.has_value()) {
        return false;
    }

    const qint64 currentId = window->noteId();
    if (windows_.value(currentId) == window) {
        windows_.remove(currentId);
    }
    windows_.insert(id, window);

    window->switchToNote(noteData.value());
    window->showNormal();
    window->raise();
    window->activateWindow();
    return true;
}

bool ApplicationController::deleteNotes(const QVector<qint64> &ids)
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
        return false;
    }

    QVector<qint64> deletedIds;
    deletedIds.reserve(normalizedIds.size());

    bool allDeleted = true;
    for (qint64 id : normalizedIds) {
        if (repository_.deleteNote(id)) {
            removeNoteAssets(id);
            deletedIds.push_back(id);
        } else {
            allDeleted = false;
        }
    }

    if (deletedIds.isEmpty()) {
        return false;
    }

    const int openCountBeforeDelete = openWindowCount();
    int deletedOpenWindowCount = 0;
    QVector<QPointer<NoteWindow>> windowsToClose;
    windowsToClose.reserve(deletedIds.size());

    for (qint64 id : deletedIds) {
        const auto it = windows_.constFind(id);
        if (it == windows_.cend() || it.value().isNull()) {
            continue;
        }
        windowsToClose.push_back(it.value());
        ++deletedOpenWindowCount;
    }

    const bool shouldOpenReplacement =
        openCountBeforeDelete > 0 && openCountBeforeDelete == deletedOpenWindowCount;

    for (const QPointer<NoteWindow> &window : windowsToClose) {
        if (window.isNull()) {
            continue;
        }
        window->prepareForDeletion();
        QTimer::singleShot(0, window, &QWidget::close);
    }

    emit notesChanged();

    if (shouldOpenReplacement) {
        QTimer::singleShot(0, this, [this]() {
            const qint64 nextId = latestNoteId();
            if (nextId >= 0) {
                openNote(nextId);
            } else {
                createAndOpenNote();
            }
        });
    }

    return allDeleted;
}

std::optional<NoteData> ApplicationController::note(qint64 id)
{
    return repository_.noteById(id);
}

QVector<NoteSummary> ApplicationController::noteSummaries()
{
    return repository_.noteSummaries(sortMode());
}

qint64 ApplicationController::latestNoteId()
{
    return repository_.startupNoteId();
}

qint64 ApplicationController::startupNoteId()
{
    switch (startupNoteMode()) {
    case StartupNoteMode::LastClosed: {
        const qint64 id = settings_.value(QLatin1StringView(kLastClosedNoteIdKey), -1).toLongLong();
        if (id >= 0 && repository_.noteById(id).has_value()) {
            return id;
        }
        return repository_.startupNoteId();
    }
    case StartupNoteMode::LastCreated:
        return repository_.latestCreatedNoteId();
    case StartupNoteMode::LastEdited:
    default:
        return repository_.startupNoteId();
    }
}

int ApplicationController::openWindowCount() const
{
    int count = 0;
    for (auto it = windows_.cbegin(); it != windows_.cend(); ++it) {
        if (!it.value().isNull()) {
            ++count;
        }
    }
    return count;
}

SortMode ApplicationController::sortMode() const
{
    return static_cast<SortMode>(
        settings_.value(QLatin1StringView(kSortModeKey),
                        static_cast<int>(SortMode::LastEditedDesc))
            .toInt());
}

void ApplicationController::setSortMode(SortMode sortMode)
{
    settings_.setValue(QLatin1StringView(kSortModeKey), static_cast<int>(sortMode));
    emit notesChanged();
}

StartupNoteMode ApplicationController::startupNoteMode() const
{
    return static_cast<StartupNoteMode>(
        settings_.value(QLatin1StringView(kStartupModeKey),
                        static_cast<int>(StartupNoteMode::LastClosed))
            .toInt());
}

void ApplicationController::setStartupNoteMode(StartupNoteMode mode)
{
    settings_.setValue(QLatin1StringView(kStartupModeKey), static_cast<int>(mode));
}

QString ApplicationController::globalFontFamily() const
{
    return settings_.value(QLatin1StringView(kGlobalFontFamilyKey)).toString().trimmed();
}

int ApplicationController::globalFontPointSize() const
{
    return settings_.value(QLatin1StringView(kGlobalFontPointSizeKey), kDefaultFontPointSize).toInt();
}

void ApplicationController::setGlobalFontSettings(const QString &fontFamily, int fontPointSize)
{
    const QString nextFamily = fontFamily.trimmed();
    const int nextSize = qBound(10, fontPointSize, 40);
    const bool changed = nextFamily != globalFontFamily() || nextSize != globalFontPointSize();

    settings_.setValue(QLatin1StringView(kGlobalFontFamilyKey), nextFamily);
    settings_.setValue(QLatin1StringView(kGlobalFontPointSizeKey), nextSize);
    registerRecentFont(nextFamily);

    if (changed) {
        emit globalFontSettingsChanged(nextFamily, nextSize);
    }
}

void ApplicationController::rememberClosedNote(qint64 id)
{
    if (id >= 0) {
        settings_.setValue(QLatin1StringView(kLastClosedNoteIdKey), id);
    }
}

QStringList ApplicationController::recentFonts() const
{
    return settings_.value(QLatin1StringView(kRecentFontsKey)).toStringList();
}

void ApplicationController::registerRecentFont(const QString &family)
{
    const QString trimmed = family.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QStringList fonts = recentFonts();
    fonts.removeAll(trimmed);
    fonts.prepend(trimmed);
    while (fonts.size() > kMaxRecentFonts) {
        fonts.removeLast();
    }

    settings_.setValue(QLatin1StringView(kRecentFontsKey), fonts);
}

void ApplicationController::saveTitle(qint64 id, const QString &title)
{
    if (repository_.updateTitle(id, title)) {
        emit noteTitleChanged(id, title);
        emit notesChanged();
    }
}

void ApplicationController::saveContent(qint64 id, const QString &content)
{
    if (repository_.updateContent(id, content)) {
        emit notesChanged();
    }
}

void ApplicationController::saveAppearance(qint64 id, const QString &themeId, bool wrapMode)
{
    if (repository_.updateAppearance(id, themeId, wrapMode)) {
        emit notesChanged();
    }
}

void ApplicationController::saveGeometry(qint64 id, const QByteArray &geometry)
{
    repository_.updateGeometry(id, geometry);
}

void ApplicationController::openWindowFor(const NoteData &note)
{
    auto *window = new NoteWindow(this, note);
    windows_.insert(note.id, window);
    connect(window, &QObject::destroyed, this, [this](QObject *object) {
        removeWindow(static_cast<NoteWindow *>(object));
    });
    window->show();
    window->raise();
    window->activateWindow();
}

void ApplicationController::removeWindow(NoteWindow *window)
{
    for (auto it = windows_.begin(); it != windows_.end();) {
        if (it.value().data() == window || it.value().isNull()) {
            it = windows_.erase(it);
            continue;
        }
        ++it;
    }
}
