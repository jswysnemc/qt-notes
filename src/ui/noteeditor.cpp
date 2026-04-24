#include "ui/noteeditor.h"

#include <QWheelEvent>

NoteEditor::NoteEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setPlaceholderText(QStringLiteral("开始记录..."));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTabStopDistance(28.0);
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

    QPlainTextEdit::wheelEvent(event);
}
