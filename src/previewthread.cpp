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

#include "previewthread.h"
#include <stdio.h>
#include <kstandarddirs.h>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QMutexLocker>
#include <QTime>
#include <QSet>

PreviewThread::PreviewThread(KileDocument::LaTeXInfo* info, QObject* parent)
: QThread(parent), m_doc(info->getDoc()), m_info(info) {
	m_user = m_info->user();
	m_abort = false;
	m_dirty = false;
	connect(m_user, SIGNAL(documentChanged()), this, SLOT(textChanged()));
	connect(info, SIGNAL(inlinePreviewChanged(bool)), this, SLOT(textChanged()));
	m_nextprevimg = 1;
	m_dir = new KTempDir(KStandardDirs::locateLocal("tmp", "kile-inlinepreview"));
	m_dir->setAutoRemove(true);
	m_tempfilename = QFileInfo(m_dir->name() + "inpreview.tex").absoluteFilePath();
}

PreviewThread::~PreviewThread() {
	{
		QMutexLocker lock(&m_dirtymutex);
		m_abort = true;
		m_dirtycond.wakeOne();
	}
	wait();
	delete m_dir;
	m_dir = 0;
}

void PreviewThread::setDoc(KTextEditor::Document *doc) {
	m_doc = doc;
}

void PreviewThread::run() {
	forever {
		// Wait for dirty or abort
		{
			QMutexLocker lock(&m_dirtymutex);
			while(!m_dirty && !m_abort)
				m_dirtycond.wait(&m_dirtymutex);
			if (m_abort) {
				break;
			}
			m_newdirty = false;
		}
		
		// Run LaTeX to create Previews
		createPreviews();
		
		{
			QMutexLocker lock(&m_dirtymutex);
			if (m_newdirty) {
				continue;
			}
			m_dirty = false;
		}
		
		// Emit dirtychange
		emit dirtychanged();
	}
}

void PreviewThread::createPreviews() {
	if (!m_info->isInlinePreview())
		return;
	QList<Part*> tempenvs;
	
	m_res = m_user->data();
	
	CollectionPart *prp = m_user->preamble(m_res.doc(), m_res.text());
	QString preamble = prp->source(m_res.text());
	delete prp;
	if (preamble != lastpremable) {
		m_previmgs.clear();
		//qDebug() << "Preamble changed -> clear";
		lastpremable = preamble;
	}
	
	QSet<QString> allmaths;
	
	// Insert new math
	foreach(Part *env, m_res.mathgroups()) {
		//qDebug() << "math" << env->source(text);
		allmaths.insert(env->source(m_res.text()));
		if (!m_previmgs.contains(env->source(m_res.text()))) {
			tempenvs << env;
		}
	}
	
	for(QMap<QString,QImage>::iterator it = m_previmgs.begin(); it != m_previmgs.end(); ) {
		if (!allmaths.contains(it.key())) {
			//qDebug() << "Erase:" << it.key();
			it = m_previmgs.erase(it);
		} else
			it++;
	}
	
	binaryCreatePreviews(preamble, tempenvs, 0, tempenvs.size()-1);
}


void PreviewThread::binaryCreatePreviews (QString& preamble, QList< Part* > tempenvs, int start, int end ) {
	// Check if the document changed again in the meantime
	if (m_res.text() != m_doc->text())
		return;
	if (end < start)
		return;
	// Create LaTeX preview file
	QFile tempfile(m_tempfilename);
	tempfile.open(QIODevice::WriteOnly);
	QTextStream fout(&tempfile);
	fout << preamble;
	
	// Create preview preamble
	fout << "\\usepackage[active,delayed,displaymath,floats,textmath,graphics,tightpage,showlabels]{preview}" << endl;
	fout << "\\begin{document}" << endl;
	
	for (int i = start; i <= end; i++) {
		Part *env = tempenvs[i];
		// FIXME Double dollar signs do not work! Without the preview environment they do!
		fout << "\n\\begin{preview}\n" << env->source(m_res.text()) << "\n\\end{preview}\n" << endl << endl;
	}
	
	fout << "\\end{document}" << endl;
	tempfile.close();
	
	bool success = true;
	
	// Run LaTeX
	QProcess proc;
	proc.setWorkingDirectory(m_dir->name());
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("TEXINPUTS", ".:"+m_info->url().directory()+":");
	proc.setProcessEnvironment(env);
	proc.start("latexmk -pdf -silent inpreview.tex");
	proc.waitForFinished(-1);
	if (proc.exitCode()) {
		success = false;
	} else {
		// Run dvipng
		// The outputfiles have the following names {Number of the LaTeX-Run}-{Number of the image in this LaTeX-Run}.png
		QProcess dvipng;
		dvipng.setWorkingDirectory(m_dir->name());
		dvipng.start("convert -density 96x96 inpreview.pdf " + QString::number(m_nextprevimg) + ".png");
		dvipng.waitForFinished(-1);
		if (dvipng.exitCode()) {
			success = false;
		}
	}
	
	if (!success) {
		if (start != end) {
			binaryCreatePreviews(preamble, tempenvs, start, (start+end)/2);
			binaryCreatePreviews(preamble, tempenvs, (start+end)/2+1, end);
		} else {
			//qDebug() << "Failed:" << tempenvs[start]->source(m_res.text());
			m_previmgs[tempenvs[start]->source(m_res.text())] = QImage();
		}
	} else {
		// Load images from disk
		int ipr = 1;
		for (int i = start; i <= end; i++) {
			Part *env = tempenvs[i];
			//qDebug() << "Succeeded:" << env->source(text);
			QString filename = m_dir->name() + "/" + QString::number(m_nextprevimg) + (end>start ? "-" + QString::number(ipr-1) : "") + ".png";
			m_previmgs[env->source(m_res.text())] = QImage(filename);
			ipr++;
		}
		m_nextprevimg++;
	}
}


void PreviewThread::textChanged() {
	if (m_info->isInlinePreview()) {
		bool dirbef;
		{
			QMutexLocker lock(&m_dirtymutex);
			dirbef = m_dirty;
			m_dirty = true;
			m_newdirty = true;
			m_dirtycond.wakeOne();
		}
		if (!dirbef)
			emit dirtychanged();
	} else {
		m_previmgs.clear();
		emit dirtychanged();
	}
}

bool PreviewThread::startquestions() {
	m_dirtymutex.lock();
	if (!m_dirty && m_res.text() == m_doc->text()) {
		return true;
	} else {
		m_dirtymutex.unlock();
		return false;
	}
}

void PreviewThread::endquestions() {
	m_dirtymutex.unlock();
}

QList<Part*> PreviewThread::mathpositions() {
	return m_user->getMathgroups(m_res.doc(),m_res.text());
}

QImage PreviewThread::image(Part* part) {
	return m_previmgs[part->source(m_res.text())];
}

QMap<QString,QImage> PreviewThread::images() {
	return m_previmgs;
}

QString PreviewThread::parsedText() {
	return m_res.text();
}

#include "previewthread.moc"
