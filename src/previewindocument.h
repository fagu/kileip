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

#include <cassert>
#include <map>
#include <set>
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
class PreviewWidgetHandler;

class PreviewWidget : public QWidget {
Q_OBJECT
public:
    PreviewWidget(ViewHandler* viewHandler, const KTextEditor::Range &range, QString text, PreviewWidgetHandler* widget_handler);
    ~PreviewWidget();
    void setRange(const KTextEditor::Range &range);
    image_state img() const {return m_img;}
    void setPicture(const image_state& img);
    KTextEditor::Range range() const {return m_range;}
    QString text() const {return m_text;}
    // This updates the text and, if necessary, relinks the widget to the correct formula cache.
    void updateText();
public Q_SLOTS:
    // Recompute the display data.
    void updateData();
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    ViewHandler *vh;
    image_state m_img;
    KTextEditor::Range m_range;
    QString m_text;
    PreviewWidgetHandler* m_widget_handler;
    // Iterator pointing to the corresponding element of m_widget_handler.m_formulas[m_text].widgets.
    std::optional<std::list<PreviewWidget*>::iterator> m_widget_iterator;
    
    struct image_too_large {
        friend bool operator==(const image_too_large&, const image_too_large&) {
            return true;
        }
        friend bool operator!=(const image_too_large&, const image_too_large&) {
            return false;
        }
    };
    struct Image {
        std::shared_ptr<QImage> img;
        QRect rect;
        friend bool operator==(const Image& a, const Image& b) {
            return a.img == b.img && a.rect == b.rect;
        }
        friend bool operator!=(const Image& a, const Image& b) {
            return !(a == b);
        }
    };
    // This struct encapsulates the information necessary to draw the widget.
    struct DisplayData {
        QPoint pos; // top left corner of the widget
        QRegion region; // the region this widget covers, relative to pos
        QList<QLine> border; // the region's border, relative to pos
        std::variant<Image,image_dirty,image_error,image_too_large> image; // the image
        void normalize();
        friend bool operator==(const DisplayData& a, const DisplayData& b) {
            return a.pos == b.pos && a.region == b.region && a.border == b.border && a.image == b.image;
        }
        friend bool operator!=(const DisplayData& a, const DisplayData& b) {
            return !(a == b);
        }
    };
    // nil means invisible
    std::optional<DisplayData> m_display_data;
    void setData(const std::optional<DisplayData>& display_data);
    void applyData(const std::optional<DisplayData>& display_data);
};

struct FormulaCache {
    // The cached image.
    image_state img;
    // List of widgets presenting this formula. They will be notified when the image changes. We use std::list for efficient removal.
    std::list<PreviewWidget*> widgets;
    // Iterator pointing to the corresponding element of the gc queue `m_formula_generations'.
    std::optional<std::list<QString>::iterator> generation_iterator;
    FormulaCache() : img(image_dirty()) {}
    ~FormulaCache() {
        // You shouldn't delete a formula cache if there's still a widget using it.
        assert(widgets.empty());
    }
};

class PreviewWidgetHandler : public QObject {
Q_OBJECT
public:
    PreviewWidgetHandler(KTextEditor::View *view, KileDocument::LaTeXInfo *info);
    // Remove the widget for the given iterator from the formula cache.
    // This might add the formula to m_formula_generations.
    void unlink_widget(std::list<PreviewWidget*>::iterator it);
    // Add the given widget to the formula cache. Return the new iterator.
    // This might remove the formula from m_formula_generations.
    // The formula must already exist in the cache. The widget is notified of the picture.
    std::list<PreviewWidget*>::iterator link_widget(PreviewWidget* widget);
private:
    KileDocument::LaTeXInfo *m_info;
    ViewHandler vh;
    
    PreviewThread m_thread;
    
    // The lines in the document.
    std::vector<QString> m_lines;
    // The parser states in the document. There are nr_of_lines + 1 states.
    // m_states[i] is the state *before* line i.
    // m_states[nr_of_lines] is the state after parsing the entire document.
    std::vector<State> m_states;
    // Cursor right before \begin{document}.
    std::unique_ptr<KTextEditor::MovingCursor> m_begin_document;
    // Text before \begin{document}. The entire document if there's no \begin{document}.
    QString m_preamble;
    
    // The cache of images for formulas. This also includes references to the corresponding widgets, which need to be notified when pictures are updated.
    // We use std::unique_ptr to make sure that the FormulaCache is always moved, not copied.
    std::map<QString,std::unique_ptr<FormulaCache> > m_formulas;
    // The currently cached but unused formulas, ordered by the last generation they were used in. (gc queue)
    std::list<QString> m_formula_generations;
    // List of formulas that have become unused since the last garbage collector run. Unused formulas whose picture hasn't been computed, yet, will be removed by the garbage collector, also from the queue.
    std::vector<QString> m_newly_unused;
    
    // m_widgets_by_line[i]: list of widgets whose formula ends in line i.
    // This has to be declared AFTER m_formulas and m_formula_generations so it is destructed first. The destructor of PreviewWidget calls unlink_widget().
    std::vector<std::vector<std::unique_ptr<PreviewWidget> > > m_widgets_by_line;
    
    long long m_used_bytes = 0;
    long long m_unused_bytes = 0;
    
    // Formulas to be scheduled for pdflatex.
    std::vector<QString> m_to_enqueue;
    
    void setBeginDocument(std::optional<KTextEditor::Cursor> cursor);
    void updatePreamble();
    void do_enqueue();
    
    // Add a formula to the cache, with a WIP image. Also add it to the queue for pdflatex. The formula is immediately added to the gc queue.
    void addFormula(const QString& text);
    
    // Update a picture. This will notify all dependent widgets and recompute the used memory. It will ignore formulas that are not in the cache.
    void updatePicture(const QString& text, image_state img);
    
    // Clears the entire cache and pdflatex queue. Then, re-adds all used formulas to pdflatex queue.
    void resetPictures();
    
    // Reparse the entire document.
    void reload();
    // Reparse only the given line.
    bool reloadLine(int linenr);
    // Reparse the line and, if necessary, the following lines.
    void reloadLineAuto(int linenr);
    // Handle a (re)parsed line.
    void updateWidgets(int linenr, const ParsedLine& mathenv);
    
    // An very rough approximation for the amount of memory used by the cache entry (excluding widgets).
    long long cacheEntryByteSize(const QString& text) const;
    
    // Remove old unused formulas from the cache. We want to keep the youngest formulas in the cache in case the user undoes the last operation(s).
    // We keep removing the oldest formula until the wasted memory `m_unused_bytes' is at most as large as the currently necessary memory `m_used_bytes'.
    void collect_garbage();
    
public Q_SLOTS:
    // Insert / update the given pictures into / in the cache.
    // Also runs the garbage collector.
    void updatePictures(QString preamble, VPSI texts);
    
    // The following methods are called when the document is modified. They reparse, but do not run the garbage collector of order any runs of pdflatex.
    void textInserted(KTextEditor::Document *document, const KTextEditor::Cursor &position, const QString &text);
    void textRemoved(KTextEditor::Document *document, const KTextEditor::Range &range, const QString &text);
    void lineWrapped(KTextEditor::Document *document, const KTextEditor::Cursor &position);
    void lineUnwrapped(KTextEditor::Document *document, int linenr);
    
    // This will reparse the entire document, run the garbage collector, and order pdflatex.
    void reloaded(KTextEditor::Document * document);
    // This will run the garbage collector, and order pdflatex.
    void editingFinished(KTextEditor::Document * document);
};

#endif
