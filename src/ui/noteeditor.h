#pragma once

#include <QPlainTextEdit>

class NoteEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit NoteEditor(QWidget *parent = nullptr);

signals:
    void fontZoomRequested(int steps);

protected:
    void wheelEvent(QWheelEvent *event) override;
};
