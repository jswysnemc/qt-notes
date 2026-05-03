#include "ui/notelistpopup.h"

#include <algorithm>
#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include "model/notelistmodel.h"

namespace {

class NoteListDelegate : public QStyledItemDelegate
{
public:
    explicit NoteListDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setTheme(const ThemeSpec &theme)
    {
        theme_ = theme;
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QRect card = option.rect.adjusted(6, 4, -6, -4);
        QColor background = Qt::transparent;
        if (option.state.testFlag(QStyle::State_Selected)
            || option.state.testFlag(QStyle::State_MouseOver)) {
            background = theme_.hoverColor;
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(card, 10, 10);

        const QRect titleRect = card.adjusted(10, 8, -10, -18);
        const QRect timeRect = card.adjusted(10, 26, -10, -6);
        const bool encrypted = index.data(NoteListModel::EncryptedRole).toBool();
        const bool recoveryRequired = index.data(NoteListModel::RecoveryRequiredRole).toBool();
        QRect adjustedTitleRect = titleRect;

        if (encrypted) {
            const QString badgeText =
                recoveryRequired ? QStringLiteral("已锁定") : QStringLiteral("已加密");
            const QFontMetrics badgeMetrics(option.font);
            const int badgeWidth = badgeMetrics.horizontalAdvance(badgeText) + 14;
            const QRect badgeRect(card.right() - badgeWidth - 10, card.top() + 8, badgeWidth, 20);

            adjustedTitleRect.setRight(badgeRect.left() - 10);
            painter->setPen(Qt::NoPen);
            painter->setBrush(recoveryRequired ? QColor(QStringLiteral("#B94A48"))
                                               : theme_.borderColor);
            painter->drawRoundedRect(badgeRect, 10, 10);
            painter->setPen(QColor(QStringLiteral("#FFF8F8")));
            painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
        }

        QFont titleFont = option.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(theme_.textColor);
        painter->drawText(adjustedTitleRect,
                          Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                          index.data(NoteListModel::TitleRole).toString());

        painter->setFont(option.font);
        painter->setPen(theme_.mutedTextColor);
        painter->drawText(timeRect,
                          Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                          QStringLiteral("编辑于 %1")
                              .arg(displayTimestamp(index.data(NoteListModel::UpdatedAtRole)
                                                        .toLongLong())));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(260, 58);
    }

private:
    ThemeSpec theme_;
};

} // namespace

NoteListPopup::NoteListPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setObjectName(QStringLiteral("noteListPopup"));
    resize(340, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    emptyLabel_ = new QLabel(QStringLiteral("还没有便签"), this);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(emptyLabel_);

    listView_ = new QListView(this);
    listView_->setMouseTracking(true);
    listView_->setFrameShape(QFrame::NoFrame);
    listView_->setSelectionMode(QAbstractItemView::NoSelection);
    listView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView_->setSpacing(2);
    listView_->setContextMenuPolicy(Qt::CustomContextMenu);

    model_ = new NoteListModel(this);
    auto *delegate = new NoteListDelegate(this);
    listView_->setItemDelegate(delegate);
    listView_->setModel(model_);
    layout->addWidget(listView_);

    auto *actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    openButton_ = new QPushButton(QStringLiteral("打开选中"), this);
    selectionModeButton_ = new QPushButton(QStringLiteral("选择"), this);
    selectAllButton_ = new QPushButton(QStringLiteral("全选"), this);
    deleteButton_ = new QPushButton(QStringLiteral("删除选中"), this);
    deleteButton_->setObjectName(QStringLiteral("dangerButton"));

    actionsLayout->addWidget(openButton_, 1);
    actionsLayout->addWidget(selectionModeButton_);
    actionsLayout->addWidget(selectAllButton_);
    actionsLayout->addWidget(deleteButton_);
    layout->addLayout(actionsLayout);

    browseClickTimer_ = new QTimer(this);
    browseClickTimer_->setSingleShot(true);
    browseClickTimer_->setInterval(QApplication::doubleClickInterval());

    connect(listView_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        updateActionState();
    });
    connect(listView_, &QListView::clicked, this, &NoteListPopup::handleItemClicked);
    connect(listView_, &QListView::customContextMenuRequested, this, &NoteListPopup::openNoteInNewWindow);
    connect(listView_, &QListView::doubleClicked, this, &NoteListPopup::renameNoteAt);
    connect(browseClickTimer_, &QTimer::timeout, this, [this]() {
        if (pendingOpenNoteId_ >= 0) {
            openNoteInCurrentWindow(pendingOpenNoteId_);
        }
    });
    connect(openButton_, &QPushButton::clicked, this, &NoteListPopup::openSelectedNotes);
    connect(selectionModeButton_, &QPushButton::clicked, this, &NoteListPopup::toggleSelectionMode);
    connect(selectAllButton_, &QPushButton::clicked, this, &NoteListPopup::toggleSelectAllNotes);
    connect(deleteButton_, &QPushButton::clicked, this, &NoteListPopup::requestDeleteSelectedNotes);

    updateActionState();
}

void NoteListPopup::setNotes(const QVector<NoteSummary> &notes)
{
    const QVector<qint64> selectedIds = selectionMode_ ? selectedNoteIds() : QVector<qint64>();
    model_->setNotes(notes);

    const bool hasNotes = !notes.isEmpty();
    listView_->setVisible(hasNotes);
    emptyLabel_->setVisible(!hasNotes);

    if (QItemSelectionModel *selectionModel = listView_->selectionModel();
        selectionModel != nullptr) {
        const QSignalBlocker blocker(selectionModel);
        selectionModel->clearSelection();

        if (selectionMode_) {
            for (qint64 id : selectedIds) {
                const int row = model_->rowForNoteId(id);
                if (row < 0) {
                    continue;
                }
                selectionModel->select(model_->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
            }
        }
    }

    updateActionState();
}

void NoteListPopup::setTheme(const ThemeSpec &theme)
{
    theme_ = theme;
    auto *delegate = static_cast<NoteListDelegate *>(listView_->itemDelegate());
    if (delegate != nullptr) {
        delegate->setTheme(theme);
    }

    setStyleSheet(QStringLiteral(R"(
QFrame#noteListPopup {
    background: %1;
    color: %2;
    border: 1px solid %3;
    border-radius: 14px;
}
QListView {
    background: transparent;
    color: %2;
    outline: none;
}
QLabel {
    color: %4;
}
QPushButton {
    min-height: 30px;
    padding: 0 12px;
    border-radius: 10px;
    border: 1px solid %3;
    background: %6;
    color: %2;
}
QPushButton:hover {
    background: %5;
}
QPushButton:disabled {
    color: %4;
    border-color: %6;
}
QPushButton#dangerButton {
    background: #B94A48;
    border-color: #A23E3C;
    color: #FFF8F8;
}
QPushButton#dangerButton:hover {
    background: #C95A58;
}
QPushButton#dangerButton:disabled {
    background: #8F4C4A;
    border-color: #8F4C4A;
    color: #E8C5C4;
}
QScrollBar:vertical {
    width: 8px;
    background: transparent;
}
QScrollBar::handle:vertical {
    border-radius: 4px;
    background: %5;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
    border: none;
}
)")
                      .arg(theme.surfaceColor.name(),
                           theme.textColor.name(),
                           theme.borderColor.name(),
                           theme.mutedTextColor.name(),
                           theme.hoverColor.name(),
                           theme.editorColor.name()));
}

void NoteListPopup::popupBelow(QWidget *anchor)
{
    setSelectionModeEnabled(false);
    adjustSize();
    const QPoint globalBottomLeft = anchor->mapToGlobal(QPoint(0, anchor->height() + 6));
    QPoint popupPosition = QPoint(globalBottomLeft.x() - width() + anchor->width(),
                                  globalBottomLeft.y());

    if (QScreen *screen = QApplication::screenAt(globalBottomLeft)) {
        const QRect available = screen->availableGeometry();
        popupPosition.setX(qBound(available.left(),
                                  popupPosition.x(),
                                  available.right() - width()));
        popupPosition.setY(qBound(available.top(),
                                  popupPosition.y(),
                                  available.bottom() - height()));
    }

    move(popupPosition);
    show();
    raise();
    listView_->setFocus();
}

void NoteListPopup::setSelectionModeEnabled(bool enabled)
{
    selectionMode_ = enabled;
    pendingOpenNoteId_ = -1;
    browseClickTimer_->stop();
    listView_->setSelectionMode(enabled ? QAbstractItemView::MultiSelection
                                        : QAbstractItemView::NoSelection);

    if (QItemSelectionModel *selectionModel = listView_->selectionModel();
        selectionModel != nullptr) {
        selectionModel->clearSelection();
    }

    updateActionState();
}

QVector<qint64> NoteListPopup::selectedNoteIds() const
{
    QVector<qint64> ids;
    if (listView_->selectionModel() == nullptr) {
        return ids;
    }

    QModelIndexList indexes = listView_->selectionModel()->selectedRows();
    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &left, const QModelIndex &right) {
        return left.row() < right.row();
    });

    ids.reserve(indexes.size());
    for (const QModelIndex &index : indexes) {
        const qint64 id = index.data(NoteListModel::IdRole).toLongLong();
        if (id >= 0) {
            ids.push_back(id);
        }
    }
    return ids;
}

void NoteListPopup::updateActionState()
{
    const bool hasNotes = model_->rowCount() > 0;
    const int selectedCount = selectedNoteIds().size();
    const bool hasSelection = selectedCount > 0;
    const bool allSelected = hasNotes && selectedCount == model_->rowCount();

    openButton_->setVisible(selectionMode_);
    selectAllButton_->setVisible(selectionMode_);
    deleteButton_->setVisible(selectionMode_);

    openButton_->setEnabled(selectionMode_ && hasSelection);
    deleteButton_->setEnabled(selectionMode_ && hasSelection);
    selectAllButton_->setEnabled(selectionMode_ && hasNotes);
    selectAllButton_->setText(allSelected ? QStringLiteral("全不选") : QStringLiteral("全选"));

    selectionModeButton_->setVisible(hasNotes);
    selectionModeButton_->setText(selectionMode_ ? QStringLiteral("完成")
                                                 : QStringLiteral("选择"));
}

void NoteListPopup::handleItemClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    if (selectionMode_) {
        updateActionState();
        return;
    }

    pendingOpenNoteId_ = index.data(NoteListModel::IdRole).toLongLong();
    browseClickTimer_->start();
}

void NoteListPopup::toggleSelectionMode()
{
    setSelectionModeEnabled(!selectionMode_);
}

void NoteListPopup::toggleSelectAllNotes()
{
    if (!selectionMode_ || model_->rowCount() <= 0 || listView_->selectionModel() == nullptr) {
        return;
    }

    if (selectedNoteIds().size() == model_->rowCount()) {
        listView_->selectionModel()->clearSelection();
    } else {
        listView_->selectAll();
    }

    updateActionState();
}

void NoteListPopup::openNoteInNewWindow(const QPoint &position)
{
    if (selectionMode_) {
        return;
    }

    browseClickTimer_->stop();
    pendingOpenNoteId_ = -1;

    const QModelIndex index = listView_->indexAt(position);
    if (!index.isValid()) {
        return;
    }

    const qint64 id = index.data(NoteListModel::IdRole).toLongLong();
    if (id < 0) {
        return;
    }

    hide();
    emit noteChosenInNewWindow(id);
}

void NoteListPopup::openNoteInCurrentWindow(qint64 id)
{
    if (id < 0) {
        return;
    }

    pendingOpenNoteId_ = -1;
    hide();
    emit noteChosenInCurrentWindow(id);
}

void NoteListPopup::openSelectedNotes()
{
    const QVector<qint64> ids = selectedNoteIds();
    if (ids.isEmpty()) {
        return;
    }

    hide();
    emit notesChosen(ids);
}

void NoteListPopup::requestDeleteSelectedNotes()
{
    const QVector<qint64> ids = selectedNoteIds();
    if (ids.isEmpty()) {
        return;
    }

    hide();
    emit deleteRequested(ids);
}

void NoteListPopup::renameNoteAt(const QModelIndex &index)
{
    if (!index.isValid() || selectionMode_) {
        return;
    }

    browseClickTimer_->stop();
    pendingOpenNoteId_ = -1;

    const qint64 noteId = index.data(NoteListModel::IdRole).toLongLong();
    const QString currentTitle = index.data(NoteListModel::TitleRole).toString().trimmed();
    const bool encrypted = index.data(NoteListModel::EncryptedRole).toBool();

    hide();

    if (encrypted) {
        QMessageBox::information(parentWidget(),
                                 QStringLiteral("便签已加密"),
                                 QStringLiteral("加密便签需要先在对应窗口里解锁后再修改标题。"));
        return;
    }

    bool accepted = false;
    const QString nextTitle = QInputDialog::getText(parentWidget(),
                                                    QStringLiteral("重命名便签"),
                                                    QStringLiteral("便签标题"),
                                                    QLineEdit::Normal,
                                                    currentTitle,
                                                    &accepted)
                                  .trimmed();
    if (!accepted || nextTitle.isEmpty() || nextTitle == currentTitle) {
        return;
    }

    emit renameRequested(noteId, nextTitle);
}
