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

#include <map>
#include <variant>
#include <QWidget>
#include <ktexteditor/view.h>
#include <ktexteditor/movingrangefeedback.h>
#include <ktexteditor/movingcursor.h>
#include <ktexteditor/movingrange.h>
#include <ktexteditor/movinginterface.h>
#include "documentinfo.h"
#include "previewthread.h"
#include "previewpreparer.h"

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

class PreviewWidget;
typedef std::multimap<QString,PreviewWidget*> WIDGETS;

class PreviewWidget : public QWidget, KTextEditor::MovingRangeFeedback {
friend class PreviewWidgetUpdateRect;
Q_OBJECT
public:
    PreviewWidget(ViewHandler* viewHandler, const image_state& img, const KTextEditor::Range &range, QString text, WIDGETS* widgets_by_text);
    ~PreviewWidget();
    void setRange(const KTextEditor::Range &range);
    void caretEnteredRange (KTextEditor::MovingRange* range, KTextEditor::View* view) override;
    void caretExitedRange (KTextEditor::MovingRange* range, KTextEditor::View* view) override;
    image_state img() {return m_img;}
    void setPicture(const image_state& img);
    KTextEditor::Range range() const {return m_range->toRange();}
    void setWidgetsByTextIt(WIDGETS::iterator it) {m_widgets_by_text_it = it;}
    bool setText(const QString& text);
public Q_SLOTS:
    void updateRect();
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    bool m_cursor;
    ViewHandler *vh;
    image_state m_img;
    std::unique_ptr<KTextEditor::MovingRange> m_range;
    QString m_text;
    bool m_dirty;
    QList<QLine> m_border;
    QRect m_imgrect;
    WIDGETS* m_widgets_by_text;
    std::optional<WIDGETS::iterator> m_widgets_by_text_it;
};

class PreviewWidgetHandler : public QObject {
Q_OBJECT
public:
    PreviewWidgetHandler(KTextEditor::View *view, KileDocument::LaTeXInfo *info);
private:
    KileDocument::LaTeXInfo *m_info;
    std::unique_ptr<ViewHandler> vh;
    
    std::unique_ptr<PreviewThread> m_thread;
    
    std::vector<State> m_states;
    std::unique_ptr<KTextEditor::MovingCursor> m_begin_document;
    
    std::multimap<QString,PreviewWidget*> m_widgets_by_text;
    std::vector<std::vector<std::unique_ptr<PreviewWidget> > > m_widgets_by_line;
    
    // TODO Collect garbage
    std::map<QString,image_state> m_previmgs;
    QString m_preamble;
    std::vector<QString> to_enqueue;
    
    void setBeginDocument(std::optional<KTextEditor::Cursor> cursor);
    void updatePreamble();
    void do_enqueue();
    void reload();
    bool reloadLine(int linenr);
    void reloadLineAuto(int linenr);
    void updateWidgets(int linenr, const ParsedLine& mathenv);
public Q_SLOTS:
    void updatePictures(QString preamble, VPSI texts);
    void textInserted(KTextEditor::Document *document, const KTextEditor::Cursor &position, const QString &text);
    void textRemoved(KTextEditor::Document *document, const KTextEditor::Range &range, const QString &text);
    void lineWrapped(KTextEditor::Document *document, const KTextEditor::Cursor &position);
    void lineUnwrapped(KTextEditor::Document *document, int linenr);
    void reloaded(KTextEditor::Document * document);
    void editingFinished(KTextEditor::Document * document);
};

#endif
