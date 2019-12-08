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
        
        void run() override;
        
        void setDoc(KTextEditor::Document *doc);
        // Returns false if the contents are dirty. If they are not dirty, the text does not get reparsed as long as endquestions() is not called.
        // This function has to be called before mathpositions() and getImage().
        bool startquestions();
        void endquestions();
        QList<PPart> mathpositions();
        QImage image(PPart part);
        QHash<QString,QImage> images();
        QString parsedText();
    private:
        bool m_abort; // whether to abort the thread
        
        KTextEditor::Document *m_doc;
        KileDocument::LaTeXInfo* m_info;
        
        QMutex m_dirtymutex;
        bool m_dirty;
        bool m_newdirty;
        QWaitCondition m_dirtycond;
        
        // The parser threads
        User *m_user;
        User *m_masteruser;
        
        // The result of parsing the current tex file
        std::shared_ptr<ParserResult> m_res;
        // The result of parsing the master tex file
        std::shared_ptr<ParserResult> m_masterres;
        
        // The temporary directory containing directories numbered 1, 2, 3, ... (one for each run of latex).
        std::unique_ptr<QTemporaryDir> m_dir;
        
        QHash<QString,QImage> m_previmgs;
        
        int m_currentrun;
        
        void createPreviews();
        void binaryCreatePreviews(QString &preamble, QList<PPart> tempenvs, int start, int end);
        
        // The preamble used to generate the current images.
        // Whenever the preamble changes, we regenerate all images.
        QString lastpreamble;
    Q_SIGNALS:
        void dirtychanged();
    public Q_SLOTS:
        void textChanged();
};

#endif
