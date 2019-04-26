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
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QProcess>
#include <QMutexLocker>
#include <QTime>
#include <QSet>
#include <poppler-qt5.h>

const bool keep_failed_folders = true;

PreviewThread::PreviewThread(KileDocument::LaTeXInfo* info, QObject* parent)
: QThread(parent), m_doc(info->getDoc()), m_info(info) {
    m_user = m_info->user();
    m_masteruser = m_info->masteruser();
    m_abort = false;
    m_dirty = false;
    connect(m_user, SIGNAL(documentChanged()), this, SLOT(textChanged()));
    connect(m_masteruser, SIGNAL(documentChanged()), this, SLOT(textChanged()));
    connect(info, SIGNAL(inlinePreviewChanged(bool)), this, SLOT(textChanged()));
    m_currentrun = 0;
    m_dir = new QTemporaryDir(QDir(QDir::tempPath()).filePath("kile-inlinepreview"));
    m_dir->setAutoRemove(true);
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
    if (!m_dir->isValid()) {
        qDebug() << "Could not create temporary directory! Not creating previews.";
        return;
    }
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
            m_res = m_user->data();
            m_masterres = m_masteruser->data();
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
    
    CollectionPart *prp = User::preamble(m_masterres.doc(), m_masterres.text());
    QString preamble = prp->source(m_masterres.text());
    delete prp;
    if (preamble != lastpreamble) {
        m_previmgs.clear();
        //qDebug() << "Preamble changed -> clear";
        lastpreamble = preamble;
    }
    
    QSet<QString> allmaths;
    
    // Insert new math
    foreach(Part *env, m_res.mathgroups()) {
//         qDebug() << "math" << env->source(m_res.text());
        QString tt = env->source(m_res.text());
        if (!allmaths.contains(tt)) {
            allmaths.insert(tt);
            if (!m_previmgs.contains(env->source(m_res.text())))
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


bool load_pages_from_pdf(QString filename, int expected_number_of_pages, QVector<QImage>& res) {
    Poppler::Document* document = Poppler::Document::load(filename);
    if (!document || document->isLocked()) {
        qDebug() << "Couldn't open PDF file " << filename << ".";
        delete document;
        return false;
    }
    if (document->numPages() != expected_number_of_pages) {
        qDebug() << "Found " << document->numPages() << " pages, expected " << expected_number_of_pages << ".";
        delete document;
        return false;
    }
    document->setRenderHint(Poppler::Document::RenderHint::Antialiasing);
    document->setRenderHint(Poppler::Document::RenderHint::TextAntialiasing);
    document->setRenderHint(Poppler::Document::RenderHint::IgnorePaperColor);
    res.clear();
    res.reserve(expected_number_of_pages);
    for (int pageNumber = 0; pageNumber < expected_number_of_pages; pageNumber++) {
        Poppler::Page* pdfPage = document->page(pageNumber);
        if (!pdfPage) {
            qDebug() << "Couldn't open page " << pageNumber << ".";
            delete pdfPage;
            delete document;
            return false;
        }
        QImage img = pdfPage->renderToImage(96, 96);
        if (img.isNull()) {
            qDebug() << "Couldn't render page " << pageNumber << ".";
            delete pdfPage;
            delete document;
            return false;
        }
        res.push_back(img);
        delete pdfPage;
    }
    delete document;
    return true;
}

void PreviewThread::binaryCreatePreviews (QString& preamble, QList< Part* > tempenvs, int start, int end ) {
    // Check if the document changed again in the meantime
    if (m_res.text() != m_doc->text())
        return;
    if (end < start)
        return;
    m_currentrun++;
    QDir top_folder(m_dir->path());
    top_folder.mkdir(QString::number(m_currentrun));
    QDir folder(top_folder.filePath(QString::number(m_currentrun)));
    QDir filedir(m_info->url().toLocalFile());
    filedir.cdUp();
    qDebug() << "Generating preview images for" << start << "--" << end << "; temporary directory" << folder.absolutePath() << "; working directory" << filedir.absolutePath();
    QTime tim;
    tim.start();
    // Create LaTeX preview file
    QString latex_filename = folder.filePath("inpreview.tex");
    QFile latex_file(latex_filename);
    latex_file.open(QIODevice::WriteOnly);
    {
        QTextStream fout(&latex_file);
        fout << preamble;
        
        // Create preview preamble
        fout << "\\usepackage[active,delayed,tightpage,showlabels,pdftex]{preview}" << endl;
        fout << "\\begin{document}" << endl;
        
        for (int i = start; i <= end; i++) {
            Part *env = tempenvs[i];
            // FIXME Double dollar signs do not work! Without the preview environment they do!
            fout << "\n\\begin{preview}\n" << env->source(m_res.text()) << "\n\\end{preview}\n" << endl << endl;
        }
        
        fout << "\\end{document}" << endl;
    }
    latex_file.close();
    
    bool success = true;
    
    // Run LaTeX
    QProcess proc;
    proc.setWorkingDirectory(folder.path());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TEXINPUTS", ".:"+filedir.absolutePath()+":");
    proc.setProcessEnvironment(env);
    proc.start("latexmk -pdf -silent inpreview.tex");
    proc.waitForFinished(15000);
    QVector<QImage> imgs;
    if (proc.exitCode()) {
        success = false;
    } else {
        if (!load_pages_from_pdf(folder.filePath("inpreview.pdf"), end-start+1, imgs))
            success = false;
    }
    
    if (!success) {
        qDebug() << "Failed:" << start << "--" << end << "(in" << tim.elapsed()*0.001 << "s)";
        if (start != end) {
            binaryCreatePreviews(preamble, tempenvs, start, (start+end)/2);
            binaryCreatePreviews(preamble, tempenvs, (start+end)/2+1, end);
        } else {
            qDebug() << "Failed code:" << tempenvs[start]->source(m_res.text());
            m_previmgs[tempenvs[start]->source(m_res.text())] = QImage();
        }
    } else {
        qDebug() << "Succeeded:" << start << "--" << end << "(in" << tim.elapsed()*0.001 << "s)";
        
        // Load images from disk
        int ipr = 0;
        for (int i = start; i <= end; i++) {
            Part *env = tempenvs[i];
            m_previmgs[env->source(m_res.text())] = imgs[ipr];
            ipr++;
        }
    }
    
    if (success || !keep_failed_folders) {
        qDebug() << "Removing temporary folder" << folder.absolutePath() << ".";
        folder.removeRecursively();
    }
    qDebug();
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
    return m_res.mathgroups();
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
