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

#ifndef PREVIEW_THREAD_H
#define PREVIEW_THREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <ktexteditor/document.h>
#include <queue>
#include <algorithm>
#include "documentinfo.h"
#include "user.h"

typedef std::pair<int,int> PII;
typedef std::pair<PII,int> PIII;

class PreviewThread : public QThread {
	Q_OBJECT
	
	public:
		PreviewThread(KileDocument::LaTeXInfo *info, QObject *parent = 0);
		~PreviewThread();
		
		void run();
		
		void setDoc(KTextEditor::Document *doc);
		// Returns false if the contents are dirty, if they are not dirty the text does not get reparsed as long as endquestions() is not called
		// This function has to be called before mathpositions() and getImage()
		bool startquestions();
		void endquestions();
		QList<Part*> mathpositions();
		QImage image(Part *part);
		QMap<QString,QImage> images();
		QString parsedText();
	private:
		bool m_abort;
		
		KTextEditor::Document *m_doc;
		KileDocument::LaTeXInfo* m_info;
		
		QMutex m_dirtymutex;
		bool m_dirty;
		bool m_newdirty;
		QWaitCondition m_dirtycond;
		
		ParserResult m_res;
		ParserResult m_masterres;
		
		QTemporaryDir *m_dir;
		QString m_tempfilename;
		
		QMap<QString,QImage> m_previmgs;
		
		int m_nextprevimg;
		
		void createPreviews();
		void binaryCreatePreviews(QString &preamble, QList<Part*> tempenvs, int start, int end);
		
		User *m_user;
		User *m_masteruser;
		
		QString lastpremable;
	Q_SIGNALS:
		void dirtychanged();
	public Q_SLOTS:
		void textChanged();
};

#endif
