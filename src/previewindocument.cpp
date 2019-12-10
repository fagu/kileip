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

#include "previewindocument.h"
#include <QPainter>
#include <QMouseEvent>
#include <QBoxLayout>
#include <ktexteditor/range.h>
#include <ktexteditor/configinterface.h>
#include <QTime>
#include <ktextedit.h>

ViewHandler::ViewHandler(KTextEditor::View* _view) : view(_view) {
    doc = view->document();
    connect(doc, SIGNAL(textChanged(KTextEditor::Document*)), this, SLOT(update()));
    connect(view, SIGNAL(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(update()));
    connect(view, SIGNAL(verticalScrollPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(update()));
    connect(view, SIGNAL(horizontalScrollPositionChanged(KTextEditor::View*)), this, SLOT(update()));
    update();
}

void ViewHandler::update() {
    visStart.setLine(-1);
    visEnd.setLine(-1);
    dy = 1E9;
    int x = 0, y = 0;
    while(true) {
        if (pos(x,y).x() != -1) {
            updateFound(x,y);
            return;
        }
        x += 100;
        while(x > doc->lineLength(y)) {
            x -= doc->lineLength(y)+1;
            y++;
            if (y >= doc->lines())
                goto weiter;
        }
    }
    weiter:
    x = y = 0;
    while(true) {
        if (pos(x,y).x() != -1) {
            updateFound(x,y);
            return;
        }
        if (!inc(x, y))
            break;
    }
}

void ViewHandler::updateFound(int x, int y) {
    while(true) {
        if (pos(x,y).x() == -1)
            break;
        if (!dec(x,y))
            break;
    }
    inc(x,y);
    visStart = KTextEditor::Cursor(y, x);
    int ly = -1;
    while(true) {
        QPoint p = pos(x,y);
        if (p.x() == -1)
            break;
        if (p.y()-ly > 0 && ly != -1)
            dy = std::min(dy, p.y()-ly);
        ly = p.y();
        if (!inc(x,y))
            break;
    }
    dec(x,y);
    visEnd = KTextEditor::Cursor(y, x);
}

bool ViewHandler::inc(int& x, int& y) {
    if (y >= 0 && x < doc->lineLength(y))
        x++;
    else {
        x = 0;
        y++;
        if (y >= doc->lines())
            return false;
    }
    return true;
}

bool ViewHandler::dec(int& x, int& y) {
    if (x > 0)
        x--;
    else {
        y--;
        if (y < 0)
            return false;
        x = doc->lineLength(y);
    }
    return true;
}

QPoint ViewHandler::pos(int x, int y) {
    return pos(KTextEditor::Cursor(y, x));
}

QPoint ViewHandler::pos(const KTextEditor::Cursor& c) {
    return view->cursorToCoordinate(c);
}




PreviewWidget::PreviewWidget(ViewHandler* viewHandler, const image_state& img, const KTextEditor::Range& range, QString text, WIDGETS* widgets_by_text): QWidget(viewHandler->view), vh(viewHandler), m_img(img), m_range(nullptr), m_text(text), m_widgets_by_text(widgets_by_text) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAutoFillBackground(true);
    connect(vh->doc, SIGNAL(textChanged(KTextEditor::Document*)), this, SLOT(updateRect()));
    connect(vh->view, SIGNAL(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(updateRect()));
    connect(vh->view, SIGNAL(verticalScrollPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(updateRect()));
    connect(vh->view, SIGNAL(horizontalScrollPositionChanged(KTextEditor::View*)), this, SLOT(updateRect()));
    setRange(range);
    updateRect();
    update();
}

PreviewWidget::~PreviewWidget() {
    if (m_widgets_by_text_it)
        m_widgets_by_text->erase(m_widgets_by_text_it.value());
}

void PreviewWidget::setRange(const KTextEditor::Range& range) {
    KTextEditor::MovingInterface* moving = qobject_cast<KTextEditor::MovingInterface*>(vh->doc);
    m_range.reset(moving->newMovingRange(range));
    m_range->setFeedback(this);
    updateRect();
}

#define PP(x,y) do { if (x < vh->doc->lineLength(y)) {x++;} else {x=0;y++;} } while(0)
#define MM(x,y) do { if (x > 0) {x--;} else {y--;x=vh->doc->lineLength(y);} } while(0)
#define POS(x,y) vh->pos(x,y)

#include <stdio.h>

void PreviewWidget::updateRect() {
    // FIXME Deadlock when region visibility is changed and cursorToCoordinate is called
    if (!vh->visStart.isValid() || vh->visStart > m_range->end() || vh->visEnd < m_range->start()) {
        setVisible(false);
        return;
    }
    QRect textAreaRect = vh->view->textAreaRect();
    int minx = textAreaRect.left();
    int maxx = textAreaRect.right();
    bool olddirty = m_dirty;
    m_dirty = vh->doc->text(*m_range) != m_text;
    KTextEditor::Cursor c1 = qMax(vh->visStart, (KTextEditor::Cursor)m_range->start());
    KTextEditor::Cursor c2 = qMin(vh->visEnd, (KTextEditor::Cursor)m_range->end());
    QList<QLine> oldborder = m_border;
    QRect oldimgrect = m_imgrect;
    m_border.clear();
    QRegion avail;
    QPoint p1 = vh->pos(c1), p2 = vh->pos(c2);
    {
        int l = c1.line(), c = c1.column()-1;
        bool ok = true;
        while (c >= 0 && vh->pos(c,l).y() == p1.y()) {
            if (!vh->doc->characterAt(KTextEditor::Cursor(l,c)).isSpace() || vh->view->cursorPosition() == KTextEditor::Cursor(l,c)) {
                ok = false;
                break;
            }
            c--;
        }
        if (ok)
            p1.setX(minx);
    }
    {
        int l = c2.line(), c = c2.column();
        bool ok = true;
        while (c < vh->doc->lineLength(l) && vh->pos(c,l).y() == p2.y()) {
            if (!vh->doc->characterAt(KTextEditor::Cursor(l,c)).isSpace() || vh->view->cursorPosition() == KTextEditor::Cursor(l,c)) {
                ok = false;
                break;
            }
            c++;
        }
        if (vh->pos(c,l).y() == p2.y() && vh->view->cursorPosition() == KTextEditor::Cursor(l,c))
            ok = false;
        if (ok)
            p2.setX(maxx);
    }
    QRectF imgrectf;
    if(p1.y() == p2.y()) {
        QRect rect(p1.x(), p1.y(), p2.x()-p1.x()+1, vh->dy+1);
        m_border.push_back(QLine(p1.x(),p1.y(), p1.x(),p1.y()+vh->dy));
        m_border.push_back(QLine(p2.x(),p1.y(), p2.x(),p1.y()+vh->dy));
        m_border.push_back(QLine(p1.x(),p1.y(), p2.x(),p1.y()));
        m_border.push_back(QLine(p1.x(),p1.y()+vh->dy, p2.x(),p1.y()+vh->dy));
        avail += rect;
        if (std::holds_alternative<std::shared_ptr<QImage> >(m_img)) {
            std::shared_ptr<QImage> img = std::get<std::shared_ptr<QImage> >(m_img);
            QRectF rec(rect);
            if ((float)rec.width()/rec.height() > (float)img->width()/img->height()) {
                float delta = rec.width()-rec.height()*(float)img->width()/img->height();
                rec.setWidth(rec.width()-delta);
                rec.translate(delta/2, 0);
            } else {
                float delta = rec.height()-rec.width()*(float)img->height()/img->width();
                rec.setHeight(rec.height()-delta);
                rec.translate(0, delta/2);
            }
            imgrectf = rec;
        }
    } else {
        if (p1.x() != maxx) {
            avail += QRect(p1.x(), p1.y(), maxx-p1.x()+1, vh->dy+1);
            m_border.push_back(QLine(p1.x(),p1.y(), p1.x(),p1.y()+vh->dy));
            if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
                m_border.push_back(QLine(maxx,p1.y(), maxx,p1.y()+vh->dy));
            m_border.push_back(QLine(p1.x(),p1.y(), maxx,p1.y()));
        }
        if (p2.y() != p1.y()+vh->dy) {
            avail += QRect(minx, p1.y()+vh->dy, maxx-minx+1, p2.y()-p1.y()-vh->dy+1);
            m_border.push_back(QLine(minx,p1.y()+vh->dy, minx,p2.y()));
            m_border.push_back(QLine(maxx,p1.y()+vh->dy, maxx,p2.y()));
        }
        m_border.push_back(QLine(minx,p1.y()+vh->dy, p1.x(),p1.y()+vh->dy));
        m_border.push_back(QLine(p2.x(),p2.y(), maxx,p2.y()));
        if (p2.x() != minx) {
            avail += QRect(minx, p2.y(), p2.x()-minx+1, vh->dy+1);
            m_border.push_back(QLine(p2.x(),p2.y(), p2.x(),p2.y()+vh->dy));
            if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
                m_border.push_back(QLine(minx,p2.y(), minx,p2.y()+vh->dy));
            m_border.push_back(QLine(minx,p2.y()+vh->dy, maxx,p2.y()+vh->dy));
        }
        for (int d1 = 0; d1 < 2; d1++) {
            for (int d2 = 0; d2 < 2; d2++) {
                int xx1 = minx, xx2 = maxx, yy1 = p1.y()+vh->dy, yy2 = p2.y();
                if (d1) {
                    xx1 = p1.x();
                    yy1 -= vh->dy;
                }
                if (d2) {
                    xx2 = p2.x();
                    yy2 += vh->dy;
                }
                if (std::holds_alternative<std::shared_ptr<QImage> >(m_img)) {
                    std::shared_ptr<QImage> img = std::get<std::shared_ptr<QImage> >(m_img);
                    QRectF rec(xx1, yy1, xx2-xx1+1, yy2-yy1+1);
                    if ((float)rec.width()/rec.height() > (float)img->width()/img->height()) {
                        float delta = rec.width()-rec.height()*(float)img->width()/img->height();
                        rec.setWidth(rec.width()-delta);
                        rec.translate(delta/2, 0);
                    } else {
                        float delta = rec.height()-rec.width()*(float)img->height()/img->width();
                        rec.setHeight(rec.height()-delta);
                        rec.translate(0, delta/2);
                    }
                    if (rec.width() > imgrectf.width())
                        imgrectf = rec;
                }
            }
        }
    }
    float scale = 1;
    if (std::holds_alternative<std::shared_ptr<QImage> >(m_img)) {
        std::shared_ptr<QImage> img = std::get<std::shared_ptr<QImage> >(m_img);
        if (imgrectf.width() > img->width()) {
            float scale = (float)img->width()/imgrectf.width();
            float dw = imgrectf.width()-scale*imgrectf.width();
            float dh = imgrectf.height()-scale*imgrectf.height();
            imgrectf.setWidth(scale*imgrectf.width());
            imgrectf.setHeight(scale*imgrectf.height());
            imgrectf.translate(dw/2, dh/2);
        }
        m_imgrect = QRect(qRound(imgrectf.left()), qRound(imgrectf.top()), qRound(imgrectf.width()), qRound(imgrectf.height()));
        scale = (float)m_imgrect.width()/img->width();
    }
    bool prev_cursor = m_cursor;
    m_cursor = m_range->contains(vh->view->cursorPosition());
    if (scale < 0.8) {
        setVisible(false);
        if (m_cursor != prev_cursor)
            update();
    } else {
        setVisible(true);
        QPoint shift = avail.boundingRect().topLeft();
        move(shift);
        m_imgrect.translate(-shift);
        QList<QLine> tmpborder = m_border;
        m_border.clear();
        foreach (QLine line, tmpborder)
            m_border.push_back(line.translated(-shift));
        avail.translate(-shift);
        setMask(avail);
        resize(avail.boundingRect().size());
        if (oldimgrect != m_imgrect || oldborder != m_border || olddirty != m_dirty || m_cursor != prev_cursor)
            update();
    }
}

void PreviewWidget::setPicture(const image_state& img) {
    m_img = img;
    updateRect();
    update();
}

bool PreviewWidget::setText(const QString& text) {
    if (m_text == text)
        return false;
    m_text = text;
    if (m_widgets_by_text_it)
        m_widgets_by_text->erase(m_widgets_by_text_it.value());
    m_widgets_by_text_it = m_widgets_by_text->emplace(text, this);
    return true;
}

void PreviewWidget::paintEvent(QPaintEvent* ) {
    if (std::holds_alternative<image_dirty>(m_img) || m_dirty || m_cursor)
        setPalette(QPalette(QColor(220,255,220,50)));
    else if (std::holds_alternative<image_error>(m_img))
        setPalette(QPalette(QColor(255,0,0,50)));
    else if (std::get<std::shared_ptr<QImage> >(m_img)->isNull())
        setPalette(QPalette(QColor(0,0,255,50)));
    else
        setPalette(QPalette(Qt::white));
    QPainter painter(this);
    painter.setPen(QColor(240,240,240));
    foreach(QLine line, m_border)
        painter.drawLine(line);
    if (std::holds_alternative<std::shared_ptr<QImage> >(m_img) && !m_dirty && !m_cursor) {
        std::shared_ptr<QImage> img = std::get<std::shared_ptr<QImage> >(m_img);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!img->isNull()) {
            painter.drawImage(m_imgrect, *img);
        }
    }
}

void PreviewWidget::caretEnteredRange(KTextEditor::MovingRange*, KTextEditor::View* view) {
    if (view == vh->view)
        updateRect();
}

void PreviewWidget::caretExitedRange(KTextEditor::MovingRange*, KTextEditor::View* view) {
    if (view == vh->view)
        updateRect();
}





PreviewWidgetHandler::PreviewWidgetHandler(KTextEditor::View* view, KileDocument::LaTeXInfo *info) : QObject(view), m_info(info), vh(new ViewHandler(view)), m_thread(new PreviewThread(m_info)) {
    qRegisterMetaType<VPSI>();
    connect(m_thread.get(), &PreviewThread::picturesAvailable, this, &PreviewWidgetHandler::updatePictures);
    m_thread->start();
//     m_thread->textChanged();
//     connect(info->user(), SIGNAL(documentChanged()), this, SLOT(updateWidgets()));
//     updateWidgets();
    connect(view->document(), &KTextEditor::Document::textInserted, this, &PreviewWidgetHandler::textInserted);
    connect(view->document(), &KTextEditor::Document::textRemoved, this, &PreviewWidgetHandler::textRemoved);
    connect(view->document(), &KTextEditor::Document::lineWrapped, this, &PreviewWidgetHandler::lineWrapped);
    connect(view->document(), &KTextEditor::Document::lineUnwrapped, this, &PreviewWidgetHandler::lineUnwrapped);
    connect(view->document(), &KTextEditor::Document::reloaded, this, &PreviewWidgetHandler::reloaded);
    connect(view->document(), &KTextEditor::Document::editingFinished, this, &PreviewWidgetHandler::editingFinished);
    reload();
    // TODO: Automatically switch to dynamic word wrapping
    // KTextEditor::ConfigInterface *conf = qobject_cast< KTextEditor::ConfigInterface* >(view);
    // qDebug() << "Keys:" << endl << conf->configKeys() << endl << endl;
}

void PreviewWidgetHandler::reload() {
    auto* doc = vh->doc;
    m_states.clear();
    m_widgets_by_line.clear();
    int nr_of_lines = doc->lines();
    State state;
    m_states.push_back(state);
    for (int linenr = 0; linenr < nr_of_lines; linenr++) {
        QString line = doc->line(linenr);
//         qDebug() << "Line" << linenr << line;
        auto p = parse(state, linenr, line);
        state = p.first;
        m_states.push_back(state);
        m_widgets_by_line.emplace_back();
        updateWidgets(linenr, p.second);
    }
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    do_enqueue();
    
//     for (int linenr = 0; linenr < nr_of_lines; linenr++) {
//         for (const auto& ra : m_mathenvs[linenr]) {
//             qDebug() << *ra;
//         }
//     }
}

bool PreviewWidgetHandler::reloadLine(int linenr) {
    auto* doc = vh->doc;
    QString line = doc->line(linenr);
//     qDebug() << "Line" << linenr << line;
    auto p = parse(m_states[linenr], linenr, line);
    bool changed = (m_states[linenr+1] != p.first);
    if (changed)
        m_states[linenr+1] = p.first;
    updateWidgets(linenr, p.second);
    return changed;
}

void PreviewWidgetHandler::reloadLineAuto(int linenr) {
    auto* doc = vh->doc;
    int nr_of_lines = doc->lines();
    for (; linenr < nr_of_lines; linenr++) {
        if (!reloadLine(linenr))
            break;
    }
}

void PreviewWidgetHandler::updateWidgets(int linenr, const ParsedLine& parsedline) {
    if (parsedline.begin_document)
        setBeginDocument(parsedline.begin_document);
    
    std::map<KTextEditor::Range, std::unique_ptr<PreviewWidget> > old;
    for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[linenr]) {
        KTextEditor::Range range = widget->range();
        if (!old.count(range)) {
            old.emplace(range, std::move(widget));
        }
    }
    m_widgets_by_line[linenr].clear();
    for (const MathEnv& mathenv : parsedline.mathenvs) {
        KTextEditor::Range range = mathenv.range();
        QString text = vh->doc->text(range);
        if (old.count(range)) {
            PreviewWidget* widget = old.at(range).get();
            m_widgets_by_line[linenr].emplace_back(std::move(old.at(range)));
            if (widget->setText(text)) { // text changed
                if (!m_previmgs.count(text)) {
                    m_previmgs.emplace(text, image_dirty());
                    to_enqueue.push_back(text);
                }
                widget->setPicture(m_previmgs.at(text));
            }
        } else {
            if (!m_previmgs.count(text)) {
                m_previmgs.emplace(text, image_dirty());
                to_enqueue.push_back(text);
            }
            PreviewWidget* widget = new PreviewWidget(vh.get(), m_previmgs.at(text), range, text, &m_widgets_by_text);
            m_widgets_by_line[linenr].emplace_back(widget);
            widget->setWidgetsByTextIt(m_widgets_by_text.emplace(text, widget));
        }
    }
}

void PreviewWidgetHandler::setBeginDocument(std::optional<KTextEditor::Cursor> cursor) {
    if (cursor) {
        if (!m_begin_document || m_begin_document->toCursor() != cursor) {
            KTextEditor::MovingInterface* moving = qobject_cast<KTextEditor::MovingInterface*>(vh->doc);
            m_begin_document.reset(moving->newMovingCursor(cursor.value()));
            updatePreamble();
        }
    } else {
        if (m_begin_document) {
            m_begin_document.reset();
            updatePreamble();
        }
    }
}

void PreviewWidgetHandler::updatePreamble() {
    QString preamble = vh->doc->text(KTextEditor::Range(KTextEditor::Cursor(0,0), m_begin_document ? m_begin_document->toCursor() : vh->doc->documentEnd()));
    if (m_preamble != preamble) {
        m_preamble = preamble;
        m_thread->setPreamble(preamble);
        QString prevmath;
        for (const auto& p : m_widgets_by_text) {
            QString math = p.first;
            PreviewWidget* widget = p.second;
            if (prevmath != math) {
                prevmath = math;
                to_enqueue.push_back(math);
            }
            widget->setPicture(image_dirty());
        }
    }
}

void PreviewWidgetHandler::do_enqueue() {
    m_thread->enqueue(to_enqueue);
    to_enqueue.clear();
}

void PreviewWidgetHandler::textInserted(KTextEditor::Document*, const KTextEditor::Cursor& position, const QString&) {
//     qDebug() << "textInserted(" << position << text << ")";
//     QTime tim; tim.start();
    int linenr = position.line();
    reloadLineAuto(linenr);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || position <= m_begin_document->toCursor())
        updatePreamble();
//     qDebug() << " time:" << tim.elapsed()*0.001;
}

void PreviewWidgetHandler::textRemoved(KTextEditor::Document*, const KTextEditor::Range& range, const QString&) {
//     qDebug() << "textRemoved(" << range << text << ")";
    int linenr = range.start().line();
    assert(linenr == range.end().line());
    reloadLineAuto(linenr);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || range.start() <= m_begin_document->toCursor())
        updatePreamble();
}

void PreviewWidgetHandler::lineWrapped(KTextEditor::Document*, const KTextEditor::Cursor& position) {
//     qDebug() << "lineWrapped(" << position << ")";
    int linenr = position.line();
    // TODO This is very inefficient.
    m_states.insert(m_states.begin() + linenr + 1, m_states[linenr + 1]);
    m_widgets_by_line.emplace(m_widgets_by_line.begin() + linenr);
    reloadLine(linenr);
    reloadLineAuto(linenr + 1);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || position <= m_begin_document->toCursor())
        updatePreamble();
}

void PreviewWidgetHandler::lineUnwrapped(KTextEditor::Document*, int linenr) {
//     qDebug() << "lineUnwrapped(" << linenr << ")";
    assert(linenr >= 1);
    // TODO This is very inefficient.
    m_states.erase(m_states.begin() + linenr + 1);
    m_widgets_by_line.erase(m_widgets_by_line.begin() + linenr);
    reloadLine(linenr - 1);
    reloadLineAuto(linenr);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || linenr-1 <= m_begin_document->line())
        updatePreamble();
}

void PreviewWidgetHandler::reloaded(KTextEditor::Document*) {
    qDebug() << "reloaded()";
    reload();
}

void PreviewWidgetHandler::editingFinished(KTextEditor::Document*) {
//     qDebug() << "editingFinished()";
    do_enqueue();
}

void PreviewWidgetHandler::updatePictures(QString preamble, VPSI texts) {
    if (preamble != m_preamble) // Was compiled with an older preamble
        return;
    for (const auto& p : texts) {
        const QString& text = p.first;
        const image_state& img = p.second;
        m_previmgs[text] = img;
        auto range = m_widgets_by_text.equal_range(text);
        for (auto it = range.first; it != range.second; ++it) {
            PreviewWidget* widget = it->second;
            widget->setPicture(img);
        }
    }
}
