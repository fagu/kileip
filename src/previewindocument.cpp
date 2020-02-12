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

typedef KTextEditor::Cursor Cursor;
typedef KTextEditor::Range Range;

ViewHandler::ViewHandler(KTextEditor::View* _view) : view(_view) {
    doc = view->document();
    connect(doc, &KTextEditor::Document::textChanged, this, &ViewHandler::update);
    connect(view, &KTextEditor::View::cursorPositionChanged, this, &ViewHandler::update);
    connect(view, &KTextEditor::View::verticalScrollPositionChanged, this, &ViewHandler::update);
    connect(view, &KTextEditor::View::horizontalScrollPositionChanged, this, &ViewHandler::update);
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
    visStart = Cursor(y, x);
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
    visEnd = Cursor(y, x);
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
    return pos(Cursor(y, x));
}

QPoint ViewHandler::pos(const Cursor& c) {
    return view->cursorToCoordinate(c);
}




PreviewWidget::PreviewWidget(ViewHandler* viewHandler, const Range& range, QString text, PreviewWidgetHandler* widget_handler): QWidget(viewHandler->view), vh(viewHandler), m_range(range), m_text(text), m_widget_handler(widget_handler) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAutoFillBackground(true);
    connect(vh->doc, &KTextEditor::Document::textChanged, this, &PreviewWidget::updateData);
    connect(vh->view, &KTextEditor::View::cursorPositionChanged, this, &PreviewWidget::updateData);
    connect(vh->view, &KTextEditor::View::verticalScrollPositionChanged, this, &PreviewWidget::updateData);
    connect(vh->view, &KTextEditor::View::horizontalScrollPositionChanged, this, &PreviewWidget::updateData);
    applyData(std::nullopt);
    m_widget_iterator = m_widget_handler->link_widget(this);
    update();
}

PreviewWidget::~PreviewWidget() {
    if (m_widget_iterator)
        m_widget_handler->unlink_widget(m_widget_iterator.value());
}

void PreviewWidget::setRange(const Range& range) {
    if (m_range != range) {
        m_range = range;
        updateData();
    }
}

void PreviewWidget::updateData() {
    // FIXME Deadlock when region visibility is changed and cursorToCoordinate is called
    if (!vh->visStart.isValid() || vh->visStart > m_range.end() || vh->visEnd < m_range.start()) {
        setData(std::nullopt);
        return;
    }
    QRect textAreaRect = vh->view->textAreaRect();
    int minx = textAreaRect.left();
    int maxx = textAreaRect.right();
    DisplayData display_data;
    Cursor c1 = qMax(vh->visStart, (Cursor)m_range.start());
    Cursor c2 = qMin(vh->visEnd, (Cursor)m_range.end());
    QPoint p1 = vh->pos(c1), p2 = vh->pos(c2);
    {
        int l = c1.line(), c = c1.column()-1;
        bool ok = true;
        while (c >= 0 && vh->pos(c,l).y() == p1.y()) {
            if (!vh->doc->characterAt(Cursor(l,c)).isSpace() || vh->view->cursorPosition() == Cursor(l,c)) {
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
            if (!vh->doc->characterAt(Cursor(l,c)).isSpace() || vh->view->cursorPosition() == Cursor(l,c)) {
                ok = false;
                break;
            }
            c++;
        }
        if (vh->pos(c,l).y() == p2.y() && vh->view->cursorPosition() == Cursor(l,c))
            ok = false;
        if (ok)
            p2.setX(maxx);
    }
    QRectF imgrectf;
    if(p1.y() == p2.y()) {
        QRect rect(p1.x(), p1.y(), p2.x()-p1.x()+1, vh->dy+1);
        display_data.border.push_back(QLine(p1.x(),p1.y(), p1.x(),p1.y()+vh->dy));
        display_data.border.push_back(QLine(p2.x(),p1.y(), p2.x(),p1.y()+vh->dy));
        display_data.border.push_back(QLine(p1.x(),p1.y(), p2.x(),p1.y()));
        display_data.border.push_back(QLine(p1.x(),p1.y()+vh->dy, p2.x(),p1.y()+vh->dy));
        display_data.region += rect;
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
            display_data.region += QRect(p1.x(), p1.y(), maxx-p1.x()+1, vh->dy+1);
            display_data.border.push_back(QLine(p1.x(),p1.y(), p1.x(),p1.y()+vh->dy));
            if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
                display_data.border.push_back(QLine(maxx,p1.y(), maxx,p1.y()+vh->dy));
            display_data.border.push_back(QLine(p1.x(),p1.y(), maxx,p1.y()));
        }
        if (p2.y() != p1.y()+vh->dy) {
            display_data.region += QRect(minx, p1.y()+vh->dy, maxx-minx+1, p2.y()-p1.y()-vh->dy+1);
            display_data.border.push_back(QLine(minx,p1.y()+vh->dy, minx,p2.y()));
            display_data.border.push_back(QLine(maxx,p1.y()+vh->dy, maxx,p2.y()));
        }
        display_data.border.push_back(QLine(minx,p1.y()+vh->dy, p1.x(),p1.y()+vh->dy));
        display_data.border.push_back(QLine(p2.x(),p2.y(), maxx,p2.y()));
        if (p2.x() != minx) {
            display_data.region += QRect(minx, p2.y(), p2.x()-minx+1, vh->dy+1);
            display_data.border.push_back(QLine(p2.x(),p2.y(), p2.x(),p2.y()+vh->dy));
            if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
                display_data.border.push_back(QLine(minx,p2.y(), minx,p2.y()+vh->dy));
            display_data.border.push_back(QLine(minx,p2.y()+vh->dy, maxx,p2.y()+vh->dy));
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
    QRect imgrect;
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
        imgrect = QRect(qRound(imgrectf.left()), qRound(imgrectf.top()), qRound(imgrectf.width()), qRound(imgrectf.height()));
        scale = (float)imgrect.width()/img->width();
    }
    if (std::holds_alternative<image_dirty>(m_img)) {
        display_data.image = image_dirty();
    } else if (std::holds_alternative<image_error>(m_img)) {
        display_data.image = image_error();
    } else if (scale < 0.8) { // Image doesn't fit
        display_data.image = image_too_large();
    } else if (m_range.contains(vh->view->cursorPosition())) { // Cursor inside the formula
        display_data.image = image_dirty();
    } else if (std::holds_alternative<std::shared_ptr<QImage> >(m_img)) {
        display_data.image = Image{std::get<std::shared_ptr<QImage> >(m_img), imgrect};
//         display_data.image = image_dirty();
    } else {
        assert(false);
    }
    display_data.normalize();
    setData(display_data);
}

void PreviewWidget::DisplayData::normalize() {
    pos = region.boundingRect().topLeft();
    region.translate(-pos);
    QList<QLine> oldborder = border;
    border.clear();
    foreach (QLine line, oldborder)
        border.push_back(line.translated(-pos));
    if (std::holds_alternative<Image>(image))
        std::get<Image>(image).rect.translate(-pos);
}

void PreviewWidget::setData(const std::optional<DisplayData>& display_data) {
    if (m_display_data != display_data)
        applyData(display_data);
}

void PreviewWidget::applyData(const std::optional<DisplayData>& display_data) {
    m_display_data = display_data;
    if (!m_display_data) {
//         qDebug() << m_text << "invisible";
        setVisible(false);
    } else {
//         qDebug() << m_text << m_display_data->pos << m_display_data->image.index();
        setVisible(true);
        move(m_display_data->pos);
        setMask(m_display_data->region);
        resize(m_display_data->region.boundingRect().size());
        update();
    }
}

void PreviewWidget::setPicture(const image_state& img) {
    m_img = img;
    updateData();
}


void PreviewWidget::paintEvent(QPaintEvent* ) {
    if (!m_display_data) // Not visible
        return;
    // Background color
    if (std::holds_alternative<image_dirty>(m_display_data.value().image))
        setPalette(QPalette(QColor(220,255,220,50))); // green
    else if (std::holds_alternative<image_error>(m_display_data.value().image))
        setPalette(QPalette(QColor(255,220,220,50))); // red
    else if (std::holds_alternative<image_too_large>(m_display_data.value().image))
        setPalette(QPalette(QColor(220,220,255,50))); // blue
    else {
        // If we make the overlay entirely opaque, we somehow get strange artefacts on scaled screens.
        // It looks like the underlying view doesn't always get repainted when it should be.
        // Making the overlay slightly transparent seems to solve the issue.
        setPalette(QPalette(QColor(255,255,255,254))); // white
    }
    QPainter painter(this);
    // Draw the border
    painter.setPen(QColor(240,240,240)); // gray
    foreach(QLine line, m_display_data->border)
        painter.drawLine(line);
    // Draw the image (if available)
    if (std::holds_alternative<Image>(m_display_data->image)) {
        const Image& img = std::get<Image>(m_display_data->image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(img.rect, *img.img);
    }
}

void PreviewWidget::updateText() {
    QString text = vh->doc->text(m_range);
    if (m_text == text)
        return;
    if (m_widget_iterator)
        m_widget_handler->unlink_widget(m_widget_iterator.value());
    m_text = text;
    m_widget_iterator = m_widget_handler->link_widget(this);
}





PreviewWidgetHandler::PreviewWidgetHandler(KTextEditor::View* view, KileDocument::LaTeXInfo *info) : QObject(view), m_info(info), vh(view), m_thread(m_info) {
    qRegisterMetaType<VPSI>();
    connect(&m_thread, &PreviewThread::picturesAvailable, this, &PreviewWidgetHandler::updatePictures);
    m_thread.start();
//     m_thread.textChanged();
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

// image_state PreviewWidgetHandler::image(const QString& text) {
//     if (!m_formulas.count(text))
//         addFormula(text);
//     return m_formulas.at(text).img;
// }

void PreviewWidgetHandler::unlink_widget(std::list<PreviewWidget *>::iterator it) {
    QString text = (*it)->text();
    FormulaCache* cache = m_formulas.at(text).get();
    std::list<PreviewWidget*>& widgets = cache->widgets;
    widgets.erase(it);
    if (widgets.empty()) {
        // Add to gc queue.
        m_formula_generations.push_back(text);
        auto it = m_formula_generations.end();
        it--;
        cache->generation_iterator = it;
        long long size = cacheEntryByteSize(text);
        m_used_bytes -= size;
        m_unused_bytes += size;
        m_newly_unused.push_back(text);
    }
}

std::list<PreviewWidget *>::iterator PreviewWidgetHandler::link_widget(PreviewWidget* widget) {
    QString text = widget->text();
    if (!m_formulas.count(text))
        addFormula(text);
    FormulaCache* cache = m_formulas.at(text).get();
    // Notify widget of the picture.
    widget->setPicture(cache->img);
    if (cache->generation_iterator) {
        // Remove from gc queue.
        m_formula_generations.erase(cache->generation_iterator.value());
        long long size = cacheEntryByteSize(text);
        m_used_bytes += size;
        m_unused_bytes -= size;
        cache->generation_iterator = std::nullopt;
    }
    std::list<PreviewWidget*>& widgets = cache->widgets;
    return widgets.insert(widgets.end(), widget);
}

long long PreviewWidgetHandler::cacheEntryByteSize(const QString& text) const {
    const FormulaCache* cache = m_formulas.at(text).get();
    long long res = 0;
    res += sizeof(FormulaCache);
    res += sizeof(QString);
    res += text.size(); // This isn't really the number of bytes in text.
    if (std::holds_alternative<std::shared_ptr<QImage> >(cache->img))
        res += std::get<std::shared_ptr<QImage> >(cache->img)->sizeInBytes();
    return res;
}

void PreviewWidgetHandler::collect_garbage() {
//     qDebug() << "collect_garbage()";
    std::vector<QString> remove_from_queue;
    for (const QString& text : m_newly_unused) {
        if (m_formulas.count(text)) {
            const FormulaCache* cache = m_formulas.at(text).get();
            // Note that we need to check that the formula is still unused. (Since the last garbage collection, the formula might have become unused, then used again.)
            if (cache->widgets.empty() && std::holds_alternative<image_dirty>(cache->img)) {
                m_unused_bytes -= cacheEntryByteSize(text);
                if (cache->generation_iterator) {
                    m_formula_generations.erase(cache->generation_iterator.value());
                }
                m_formulas.erase(text);
                remove_from_queue.push_back(text);
            }
        }
    }
    m_newly_unused.clear();
    m_thread.remove_from_queue(remove_from_queue);
    
    while(m_unused_bytes > m_used_bytes) {
        assert(!m_formula_generations.empty());
        // Remove the oldest unused formula from the cache.
        QString text = m_formula_generations.front();
//         qDebug() << "remove" << text;
        m_unused_bytes -= cacheEntryByteSize(text);
        m_formula_generations.erase(m_formula_generations.begin());
        m_formulas.erase(text);
    }
    
    // Remove unused formulas waiting for pdflatex.
    for (const QString& text : m_to_enqueue) {
        if (m_formulas.count(text)) {
            const FormulaCache* cache = m_formulas.at(text).get();
            if (cache->generation_iterator) {
//                 qDebug() << "remove" << text;
                m_unused_bytes -= cacheEntryByteSize(text);
                m_formula_generations.erase(cache->generation_iterator.value());
                m_formulas.erase(text);
            }
        }
    }
//     // Some debug output
//     std::vector<QString> used, unused;
//     for (auto& p : m_formulas) {
//         if (p.second.generation_iterator)
//             unused.push_back(p.first);
//         else
//             used.push_back(p.first);
//     }
//     assert(unused.size() == m_formula_generations.size());
//     qDebug() << "Used:" << m_used_bytes << used;
//     qDebug() << "Unused:" << m_unused_bytes << unused;
}

void PreviewWidgetHandler::addFormula(const QString& text) {
    assert(!m_formulas.count(text));
    m_formulas.emplace(text, std::make_unique<FormulaCache>());
    m_formula_generations.push_front(text);
    m_formulas.at(text)->generation_iterator = m_formula_generations.begin();
    m_unused_bytes += cacheEntryByteSize(text);
    m_to_enqueue.push_back(text);
}

void PreviewWidgetHandler::updatePicture(const QString& text, image_state img) {
    if (!m_formulas.count(text)) // Ignore obsolete formulas.
        return;
    FormulaCache* cache = m_formulas.at(text).get();
    // Update picture and memory estimates.
    long long& m_bytes = cache->generation_iterator ? m_unused_bytes : m_used_bytes;
    m_bytes -= cacheEntryByteSize(text);
    cache->img = img;
    m_bytes += cacheEntryByteSize(text);
    // Notify widgets.
    for (PreviewWidget* widget : cache->widgets)
        widget->setPicture(img);
}

void PreviewWidgetHandler::resetPictures() {
    m_formula_generations.clear();
    m_to_enqueue.clear();
    m_used_bytes = 0;
    m_unused_bytes = 0;
    for (auto it = m_formulas.begin(); it != m_formulas.end(); ) {
        if (it->second->generation_iterator) { // in gc queue
            // Remove
            it = m_formulas.erase(it);
        } else { // not in gc queue
            // Recompute
            it->second->img = image_dirty();
            m_used_bytes += cacheEntryByteSize(it->first);
            m_to_enqueue.push_back(it->first);
            it++;
        }
    }
}



void PreviewWidgetHandler::reload() {
    auto* doc = vh.doc;
    m_lines.clear();
    m_states.clear();
    m_widgets_by_line.clear();
    int nr_of_lines = doc->lines();
    State state;
    m_states.push_back(state);
    for (int linenr = 0; linenr < nr_of_lines; linenr++) {
        QString line = doc->line(linenr);
        m_lines.push_back(line);
//         qDebug() << "Line" << linenr << line;
        auto p = parse(state, linenr, line);
        state = p.first;
        m_states.push_back(state);
        m_widgets_by_line.emplace_back();
        updateWidgets(linenr, p.second);
    }
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    
    collect_garbage();
    
    do_enqueue();
    
//     for (int linenr = 0; linenr < nr_of_lines; linenr++) {
//         for (const auto& ra : m_mathenvs[linenr]) {
//             qDebug() << *ra;
//         }
//     }
}

bool PreviewWidgetHandler::reloadLine(int linenr) {
    auto* doc = vh.doc;
    QString line = doc->line(linenr);
    assert(line == m_lines[linenr]);
//     qDebug() << "Line" << linenr << line;
    auto p = parse(m_states[linenr], linenr, line);
    bool changed = (m_states[linenr+1] != p.first);
    if (changed)
        m_states[linenr+1] = p.first;
    updateWidgets(linenr, p.second);
    return changed;
}

void PreviewWidgetHandler::reloadLineAuto(int linenr) {
    auto* doc = vh.doc;
    int nr_of_lines = doc->lines();
    for (; linenr < nr_of_lines; linenr++) {
        if (!reloadLine(linenr))
            break;
    }
}

// The standard comparison operator on Range doesn't define a total ordering, so we can't use it for map indices.
// We instead use the following total ordering.
struct range_total_ordering {
    bool operator()(const Range& a, const Range& b) const {
        return std::make_pair(a.start(), a.end()) < std::make_pair(b.start(), b.end());
    }
};

void PreviewWidgetHandler::updateWidgets(int linenr, const ParsedLine& parsedline) {
    if (parsedline.begin_document)
        setBeginDocument(parsedline.begin_document);
    
    // Move widgets to a map: range -> widget.
    // If multiple widgets occupy the same range, all but one are deleted.
    std::map<Range, std::unique_ptr<PreviewWidget>, range_total_ordering > old;
    for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[linenr]) {
        Range range = widget->range();
        if (!old.count(range)) {
            // Rescue the widget.
            old.emplace(range, std::move(widget));
        }
    }
    // Delete all widgets that haven't been rescued.
    m_widgets_by_line[linenr].clear();
    
    // Match with the parser's result.
    for (const MathEnv& mathenv : parsedline.mathenvs) {
        Range range = mathenv.range();
        if (old.count(range)) { // There already exists a widget with the correct range.
            // Rescue the widget.
            assert(old.at(range)); // Otherwise, it seems like there are two formulas at the same range.
            m_widgets_by_line[linenr].emplace_back(std::move(old.at(range)));
        } else { // There doesn't exists a widget with the correct range, yet.
            // Create the widget.
            QString text = vh.doc->text(range);
            PreviewWidget* widget = new PreviewWidget(&vh, range, text, this);
            m_widgets_by_line[linenr].emplace_back(widget);
        }
    }
    
    // All widgets that haven't been rescued are deleted by the destructor of `old'.
}

void PreviewWidgetHandler::setBeginDocument(std::optional<Cursor> cursor) {
    if (cursor) {
        if (!m_begin_document || m_begin_document->toCursor() != cursor) {
            KTextEditor::MovingInterface* moving = qobject_cast<KTextEditor::MovingInterface*>(vh.doc);
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
    QString preamble = vh.doc->text(Range(Cursor(0,0), m_begin_document ? m_begin_document->toCursor() : vh.doc->documentEnd()));
    if (m_preamble != preamble) {
        m_preamble = preamble;
        m_thread.setPreamble(preamble);
        resetPictures();
    }
}

void PreviewWidgetHandler::do_enqueue() {
    std::vector<QString> to_enqueue;
    for (const QString& text : m_to_enqueue) {
        if (m_formulas.count(text)) // Do not enqueue formulas that have been removed by the garbage collector.
            to_enqueue.push_back(text);
    }
    m_to_enqueue.clear();
    m_thread.enqueue(to_enqueue);
}

Cursor cursorInsert(Cursor cur, Cursor pos, int len, bool end) {
    if (cur.line() != pos.line() || cur.column() < pos.column() || (end && cur.column() == pos.column()))
        return cur;
    else
        return {cur.line(), cur.column() + len};
}

void PreviewWidgetHandler::textInserted(KTextEditor::Document*, const Cursor& position, const QString& text) {
//     qDebug() << "textInserted(" << position << text << ")";
//     QTime tim; tim.start();
    int linenr = position.line();
    
    // Update text
    m_lines[linenr].insert(position.column(), text);
    
    // Update ranges
    int len = text.size();
    for (int l = 0; l < (int)m_widgets_by_line.size(); l++) {
        for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[l]) {
            Cursor start = cursorInsert(widget->range().start(), position, len, false);
            Cursor end = cursorInsert(widget->range().end(), position, len, true);
            bool update_text = widget->range().contains(position);
            widget->setRange({start, end});
            if (update_text)
                widget->updateText();
        }
    }
    
    reloadLineAuto(linenr);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || position <= m_begin_document->toCursor())
        updatePreamble();
//     qDebug() << " time:" << tim.elapsed()*0.001;
}

Cursor cursorRemove(Cursor cur, Range range) {
    if (cur.line() != range.start().line())
        return cur;
    else if (cur <= range.start())
        return cur;
    else if (cur <= range.end())
        return range.start();
    else
        return {cur.line(), cur.column() - range.columnWidth()};
}

void PreviewWidgetHandler::textRemoved(KTextEditor::Document*, const Range& range, const QString&) {
//     qDebug() << "textRemoved(" << range << ")";
    int linenr = range.start().line();
    assert(linenr == range.end().line());
    
    // Update text
    m_lines[linenr].remove(range.start().column(), range.columnWidth());
    
    // Update ranges
    for (int l = 0; l < (int)m_widgets_by_line.size(); l++) {
        for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[l]) {
            Cursor start = cursorRemove(widget->range().start(), range);
            Cursor end = cursorRemove(widget->range().end(), range);
            bool update_text = !widget->range().intersect(range).isEmpty();
            widget->setRange({start, end});
            if (update_text)
                widget->updateText();
        }
    }
    
    reloadLineAuto(linenr);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || range.start() <= m_begin_document->toCursor())
        updatePreamble();
}

Cursor cursorWrap(Cursor cur, Cursor position) {
    if (cur >= position) {
        if (cur.line() == position.line())
            return {cur.line() + 1, cur.column() - position.column()};
        else
            return {cur.line() + 1, cur.column()};
    } else {
        return cur;
    }
}

void PreviewWidgetHandler::lineWrapped(KTextEditor::Document*, const Cursor& position) {
//     qDebug() << "lineWrapped(" << position << ")";
    int linenr = position.line();
    
    // Update text
    QString line_old = m_lines[linenr];
    QString line1 = line_old.mid(0, position.column());
    QString line2 = line_old.mid(position.column());
    m_lines[linenr] = line1;
    m_lines.insert(m_lines.begin() + linenr + 1, line2);
    
    for (int l = 0; l < (int)m_lines.size(); l++) {
        m_states[l].lineWrapped(position);
    }
    
    // Update ranges
    for (int l = 0; l < (int)m_widgets_by_line.size(); l++) {
        for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[l]) {
            Cursor start = cursorWrap(widget->range().start(), position);
            Cursor end = cursorWrap(widget->range().end(), position);
            bool update_text = widget->range().contains(position);
            widget->setRange({start, end});
            if (update_text)
                widget->updateText();
        }
    }
    
    m_states.insert(m_states.begin() + linenr + 2, m_states[linenr + 1]);
    m_widgets_by_line.emplace(m_widgets_by_line.begin() + linenr + 1);
    reloadLine(linenr);
    reloadLineAuto(linenr + 1);
    if (!m_states.back().in_document())
        setBeginDocument(std::nullopt);
    if (!m_begin_document || position <= m_begin_document->toCursor())
        updatePreamble();
}

Cursor cursorUnwrap(Cursor cur, int linenr, int linelen) {
    if (cur.line() >= linenr) {
        if (cur.line() == linenr)
            return {cur.line() - 1, cur.column() + linelen};
        else
            return {cur.line() - 1, cur.column()};
    } else {
        return cur;
    }
}

void PreviewWidgetHandler::lineUnwrapped(KTextEditor::Document*, int linenr) {
//     qDebug() << "lineUnwrapped(" << linenr << ")";
    assert(linenr >= 1);
    
    // Update text
    m_lines[linenr-1].append(m_lines[linenr]);
    m_lines.erase(m_lines.begin() + linenr);
    
    for (int l = 0; l < (int)m_lines.size(); l++) {
        m_states[l].lineUnwrapped(linenr);
    }
    
    // Update ranges
    int linelen = m_lines[linenr-1].size();
    for (int l = 0; l < (int)m_widgets_by_line.size(); l++) {
        for (std::unique_ptr<PreviewWidget>& widget : m_widgets_by_line[l]) {
            Cursor start = cursorUnwrap(widget->range().start(), linenr, linelen);
            Cursor end = cursorUnwrap(widget->range().end(), linenr, linelen);
            bool update_text = widget->range().contains({linenr-1, linelen});
            widget->setRange({start, end});
            if (update_text)
                widget->updateText();
        }
    }
    
    m_states.erase(m_states.begin() + linenr + 1);
    
    // Join the widget lines
    std::vector<std::unique_ptr<PreviewWidget> > oldline = std::move(m_widgets_by_line[linenr]);
    m_widgets_by_line.erase(m_widgets_by_line.begin() + linenr);
    for (std::unique_ptr<PreviewWidget>& widget : oldline)
        m_widgets_by_line[linenr-1].push_back(std::move(widget));
    
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
    collect_garbage();
    
    do_enqueue();
}

void PreviewWidgetHandler::updatePictures(QString preamble, VPSI texts) {
    if (preamble != m_preamble) // Was compiled with an older preamble
        return;
    for (const auto& p : texts) {
        const QString& text = p.first;
        const image_state& img = p.second;
        updatePicture(text, img);
    }
    collect_garbage();
}
