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
	maxx = -1E9;
	update();
}

void ViewHandler::update() {
	visStart.setLine(-1);
	visEnd.setLine(-1);
	minx = 1E9; dx = 1E9; miny = 1E9; maxy = -1E9; dy = 1E9;
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
	int lx = -1, ly = -1;
	while(true) {
		QPoint p = pos(x,y);
		if (p.x() == -1)
			break;
		minx = min(minx, p.x());
		miny = min(miny, p.y());
		// TODO find a better way to determine maxx
		if (!doc->character(KTextEditor::Cursor(y,x)).isSpace())
			maxx = max(maxx, p.x());
		maxy = max(maxy, p.y());
		if (p.x()-lx > 0 && lx != -1)
			dx = min(dx, p.x()-lx);
		if (p.y()-ly > 0 && ly != -1)
			dy = min(dy, p.y()-ly);
		lx = p.x();
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




PreviewWidget::PreviewWidget(ViewHandler* viewHandler, QImage* img, const KTextEditor::Range& range): QWidget(viewHandler->view), vh(viewHandler), m_img(img), m_range(0) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_ShowWithoutActivating);
	if (!m_img->isNull())
		setPalette(QPalette(Qt::white));
	else
		setPalette(QPalette(QColor(255,0,0,50)));
	setAutoFillBackground(true);
	connect(vh->doc, SIGNAL(textChanged(KTextEditor::Document*)), this, SLOT(updateRect()));
	connect(vh->view, SIGNAL(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(updateRect()));
	connect(vh->view, SIGNAL(verticalScrollPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), this, SLOT(updateRect()));
	connect(vh->view, SIGNAL(horizontalScrollPositionChanged(KTextEditor::View*)), this, SLOT(updateRect()));
	setRange(range);
}

PreviewWidget::~PreviewWidget() {
	delete m_img;
	delete m_range;
}

void PreviewWidget::setRange(const KTextEditor::Range& range) {
	delete m_range;
	KTextEditor::MovingInterface* moving = qobject_cast<KTextEditor::MovingInterface*>(vh->doc);
	m_range = moving->newMovingRange(range);
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
			if (!vh->doc->character(KTextEditor::Cursor(l,c)).isSpace() || vh->view->cursorPosition() == KTextEditor::Cursor(l,c)) {
				ok = false;
				break;
			}
			c--;
		}
		if (ok)
			p1.setX(vh->minx);
	}
	{
		int l = c2.line(), c = c2.column();
		bool ok = true;
		while (c < vh->doc->lineLength(l) && vh->pos(c,l).y() == p2.y()) {
			if (!vh->doc->character(KTextEditor::Cursor(l,c)).isSpace() || vh->view->cursorPosition() == KTextEditor::Cursor(l,c)) {
				ok = false;
				break;
			}
			c++;
		}
		if (vh->pos(c,l).y() == p2.y() && vh->view->cursorPosition() == KTextEditor::Cursor(l,c))
			ok = false;
		if (ok)
			p2.setX(vh->maxx+vh->dx);
	}
	QRectF imgrectf;
	if(p1.y() == p2.y()) {
		QRect rect(p1.x()+3, p1.y()+2, p2.x()-p1.x()+1, vh->dy+1);
		m_border.push_back(QLine(p1.x()+3,p1.y()+2, p1.x()+3,p1.y()+2+vh->dy));
		m_border.push_back(QLine(p2.x()+3,p1.y()+2, p2.x()+3,p1.y()+2+vh->dy));
		m_border.push_back(QLine(p1.x()+3,p1.y()+2, p2.x()+3,p1.y()+2));
		m_border.push_back(QLine(p1.x()+3,p1.y()+2+vh->dy, p2.x()+3,p1.y()+2+vh->dy));
		QRectF rec(rect);
		avail += rect;
		if ((float)rec.width()/rec.height() > (float)m_img->width()/m_img->height()) {
			float delta = rec.width()-rec.height()*(float)m_img->width()/m_img->height();
			rec.setWidth(rec.width()-delta);
			rec.translate(delta/2, 0);
		} else {
			float delta = rec.height()-rec.width()*(float)m_img->height()/m_img->width();
			rec.setHeight(rec.height()-delta);
			rec.translate(0, delta/2);
		}
		imgrectf = rec;
	} else {
		if (p1.x() != vh->maxx+vh->dx) {
			avail += QRect(p1.x()+3, p1.y()+2, vh->maxx-p1.x()+vh->dx+1, vh->dy+1);
			m_border.push_back(QLine(p1.x()+3,p1.y()+2, p1.x()+3,p1.y()+2+vh->dy));
			if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
				m_border.push_back(QLine(vh->maxx+vh->dx+3,p1.y()+2, vh->maxx+vh->dx+3,p1.y()+2+vh->dy));
			m_border.push_back(QLine(p1.x()+3,p1.y()+2, vh->maxx+vh->dx+3,p1.y()+2));
		}
		if (p2.y() != p1.y()+vh->dy) {
			avail += QRect(vh->minx+3, p1.y()+vh->dy+2, vh->maxx-vh->minx+vh->dx+1, p2.y()-p1.y()-vh->dy+1);
			m_border.push_back(QLine(vh->minx+3,p1.y()+2+vh->dy, vh->minx+3,p2.y()+2));
			m_border.push_back(QLine(vh->maxx+vh->dx+3,p1.y()+2+vh->dy, vh->maxx+vh->dx+3,p2.y()+2));
		}
		m_border.push_back(QLine(vh->minx+3,p1.y()+2+vh->dy, p1.x()+3,p1.y()+2+vh->dy));
		m_border.push_back(QLine(p2.x()+3,p2.y()+2, vh->maxx+vh->dx+3,p2.y()+2));
		if (p2.x() != vh->minx) {
			avail += QRect(vh->minx+3, p2.y()+2, p2.x()-vh->minx+1, vh->dy+1);
			m_border.push_back(QLine(p2.x()+3,p2.y()+2, p2.x()+3,p2.y()+2+vh->dy));
			if (p2.y() != p1.y() + vh->dy || p1.x() < p2.x())
				m_border.push_back(QLine(vh->minx+3,p2.y()+2, vh->minx+3,p2.y()+2+vh->dy));
			m_border.push_back(QLine(vh->minx+3,p2.y()+2+vh->dy, vh->maxx+vh->dx+3,p2.y()+2+vh->dy));
		}
		for (int d1 = 0; d1 < 2; d1++) {
			for (int d2 = 0; d2 < 2; d2++) {
				int xx1 = vh->minx, xx2 = vh->maxx+vh->dx, yy1 = p1.y()+vh->dy, yy2 = p2.y();
				if (d1) {
					xx1 = p1.x();
					yy1 -= vh->dy;
				}
				if (d2) {
					xx2 = p2.x();
					yy2 += vh->dy;
				}
				QRectF rec(xx1+3, yy1+2, xx2-xx1+1, yy2-yy1+1);
				if ((float)rec.width()/rec.height() > (float)m_img->width()/m_img->height()) {
					float delta = rec.width()-rec.height()*(float)m_img->width()/m_img->height();
					rec.setWidth(rec.width()-delta);
					rec.translate(delta/2, 0);
				} else {
					float delta = rec.height()-rec.width()*(float)m_img->height()/m_img->width();
					rec.setHeight(rec.height()-delta);
					rec.translate(0, delta/2);
				}
				if (rec.width() > imgrectf.width())
					imgrectf = rec;
			}
		}
	}
	if (imgrectf.width() > m_img->width()) {
		float scale = (float)m_img->width()/imgrectf.width();
		float dw = imgrectf.width()-scale*imgrectf.width();
		float dh = imgrectf.height()-scale*imgrectf.height();
		imgrectf.setWidth(scale*imgrectf.width());
		imgrectf.setHeight(scale*imgrectf.height());
		imgrectf.translate(dw/2, dh/2);
	}
	m_imgrect = QRect(qRound(imgrectf.left()), qRound(imgrectf.top()), qRound(imgrectf.width()), qRound(imgrectf.height()));
	float scale = (float)m_imgrect.width()/m_img->width();
	if (scale < 0.8 || m_range->contains(vh->view->cursorPosition())) {
		setVisible(false);
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
		if (oldimgrect != m_imgrect || oldborder != m_border)
			update();
	}
}

void PreviewWidget::slotUpdate(bool visible, QPoint moveto, int w, int h) {
	setVisible(visible);
	move(moveto);
	resize(w, h);
}

void PreviewWidget::paintEvent(QPaintEvent* ) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	if (!m_img->isNull()) {
		painter.drawImage(m_imgrect, *m_img);
	}
	painter.setPen(QColor(240,240,240));
	foreach(QLine line, m_border)
		painter.drawLine(line);
}

void PreviewWidget::caretEnteredRange(KTextEditor::MovingRange*, KTextEditor::View* view) {
	if (view == vh->view)
		updateRect();
}

void PreviewWidget::caretExitedRange(KTextEditor::MovingRange*, KTextEditor::View* view) {
	if (view == vh->view)
		updateRect();
}






PreviewWidgetHandler::PreviewWidgetHandler(KTextEditor::View* view, KileDocument::LaTeXInfo *info) : QObject(view), m_info(info) {
	vh = new ViewHandler(view);
	m_thread = new PreviewThread(m_info);
	m_thread->start();
	m_thread->textChanged();
	connect(m_thread,SIGNAL(dirtychanged()), this, SLOT(picturesAvailable()));
	// TODO: Automatically switch to dynamic word wrapping
	// KTextEditor::ConfigInterface *conf = qobject_cast< KTextEditor::ConfigInterface* >(view);
	// qDebug() << "Keys:" << endl << conf->configKeys() << endl << endl;
}

void PreviewWidgetHandler::picturesAvailable() {
	if (m_thread->startquestions()) {
		QList<Part*> parts = m_thread->mathpositions();
		QString gestext = m_thread->parsedText();
		QMap<QString,QImage> previmgs = m_thread->images();
		QMap<QString,int> aktinds;
		m_thread->endquestions();
		
		foreach(QString text, m_widgets.uniqueKeys()) {
			if (*m_widgets.values(text)[0]->img() != previmgs[text]) { // TODO This is probably inefficient
				foreach (PreviewWidget *wid, m_widgets.values(text))
					delete wid;
				m_widgets.remove(text);
			}
		}
		
		if (m_info->isInlinePreview()) {
			QList<int> newlines;
			for (int k = 0; k < gestext.length(); k++) {
				if (gestext[k] == '\n') {
					newlines << k;
				}
			}
			
			for (int i = 0; i < parts.size(); i++) {
				Part *part = parts[i];
				
				int spos = part->start;
				int sline = qLowerBound(newlines.begin(),newlines.end(),spos)-newlines.begin();
				int scol = spos-newlines[sline-1]-1;
				
				int epos = part->end+1;
				int eline = qLowerBound(newlines.begin(),newlines.end(),epos)-newlines.begin();
				int ecol = epos-newlines[eline-1]-1;
				
				KTextEditor::Range ran(KTextEditor::Cursor(sline,scol), KTextEditor::Cursor(eline,ecol));
				
				QString text = part->source(gestext);
				//if (!previmgs[text].isNull())
				//	qDebug() << "Formula:" << text;
				//else
				//	qDebug() << "Formula (invalid):" << text;
				if (aktinds[text] >= m_widgets.count(text)) {
					QImage img = previmgs[text];
					m_widgets.insert(text, new PreviewWidget(vh, new QImage(img), ran));
				} else {
					m_widgets.values(text)[aktinds[text]]->setRange(ran);
				}
				
				aktinds[text]++;
			}
		} else {
			vh->maxx = -1E9; // FIXME This should actually happen, whenever the width of the view changes
		}
		
		foreach(QString text, m_widgets.keys()) {
			while(m_widgets.count(text) > aktinds[text]) {
				PreviewWidget *wid = m_widgets.values(text).last();
				m_widgets.remove(text, wid);
				delete wid;
			}
		}
	}
}


#include "previewindocument.moc"
