/********************************************************************************
*   Copyright (C) Fabian Gundlach 2010 (320pointsguy@googlemail.com)            *
*********************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#ifndef PREVIEWINDOCUMENT_H
#define PREVIEWINDOCUMENT_H

#include <QWidget>
#include <ktexteditor/view.h>
#include <ktexteditor/movingrangefeedback.h>
#include <ktexteditor/movingrange.h>
#include <ktexteditor/movinginterface.h>
#include "documentinfo.h"
#include "previewthread.h"

class ViewHandler : public QObject {
    Q_OBJECT
public:
    ViewHandler(KTextEditor::View *_view);
    QPoint pos(int x, int y);
    QPoint pos(const KTextEditor::Cursor& c);
    
    KTextEditor::View *view;
    KTextEditor::Document *doc;
    KTextEditor::Cursor visStart, visEnd;
    int dy;
public Q_SLOTS:
    void update();
private:
    void updateFound(int x, int y);
    bool inc(int &x, int &y);
    bool dec(int &x, int &y);
};

class PreviewWidget : public QWidget, KTextEditor::MovingRangeFeedback {
    friend class PreviewWidgetUpdateRect;
    Q_OBJECT
    public:
        PreviewWidget(ViewHandler* viewHandler, QImage* img, const KTextEditor::Range &range, QString text);
        ~PreviewWidget();
        void setRange(const KTextEditor::Range &range);
        void caretEnteredRange (KTextEditor::MovingRange* range, KTextEditor::View* view);
        void caretExitedRange (KTextEditor::MovingRange* range, KTextEditor::View* view);
        QImage *img() {return m_img;}
    public Q_SLOTS:
        void updateRect();
    protected:
        void paintEvent(QPaintEvent *event);
    private:
        ViewHandler *vh;
        QImage *m_img;
        KTextEditor::MovingRange *m_range;
        QString m_text;
        bool m_dirty;
        QList<QLine> m_border;
        QRect m_imgrect;
};

class PreviewWidgetHandler : public QObject {
    Q_OBJECT
    public:
        PreviewWidgetHandler(KTextEditor::View *view, KileDocument::LaTeXInfo *info);
        ~PreviewWidgetHandler();
    private:
        ViewHandler *vh;
        KileDocument::LaTeXInfo *m_info;
        PreviewThread *m_thread;
        QMultiMap<QString,PreviewWidget*> m_widgets;
    public Q_SLOTS:
        void picturesAvailable();
};

#endif
