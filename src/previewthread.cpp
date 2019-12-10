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

// 2: keep all
// 1: keep failed
// 0: keep none
const int keep_folders = 2;

PreviewThread::PreviewThread(KileDocument::LaTeXInfo* info, QObject* parent)
: QThread(parent), m_doc(info->getDoc()), m_info(info) {
    m_abort = false;
//     connect(info, SIGNAL(inlinePreviewChanged(bool)), this, SLOT(textChanged())); // TODO
    m_currentrun = 0;
    m_dir.reset(new QTemporaryDir(QDir(QDir::tempPath()).filePath("kile-inlinepreview")));
    m_dir->setAutoRemove(true);
}

PreviewThread::~PreviewThread() {
    {
        QMutexLocker lock(&m_queue_mutex);
        m_abort = true;
        m_queue_wait.wakeOne();
    }
    wait();
    m_dir = 0;
}

void PreviewThread::setDoc(KTextEditor::Document *doc) {
    m_doc = doc;
}

void PreviewThread::setPreamble(const QString& str) {
    QMutexLocker lock(&m_queue_mutex);
    m_preamble = str;
    while(!m_queue.empty())
        m_queue.pop();
    m_keep_trying = false;
}

void PreviewThread::enqueue(const std::vector<QString>& maths) {
    if (maths.empty())
        return;
    qDebug() << "Enqueue" << maths;
    QMutexLocker lock(&m_queue_mutex);
    for (const QString& math : maths)
        m_queue.push(math);
    m_queue_wait.wakeOne();
}


void PreviewThread::run() {
    if (!m_dir->isValid()) {
        qDebug() << "Could not create temporary directory! Not creating previews.";
        return;
    }
    forever {
        // Wait for dirty or abort
        QString preamble;
        std::vector<QString> todo;
        {
            QMutexLocker lock(&m_queue_mutex);
            while(m_queue.empty() && !m_abort)
                m_queue_wait.wait(&m_queue_mutex);
            if (m_abort)
                break;
            if (!m_info->isInlinePreview())
                continue;
            preamble = m_preamble;
            while(!m_queue.empty()) {
                todo.push_back(m_queue.front());
                m_queue.pop();
            }
            m_keep_trying = true;
        }
        
        // Run LaTeX to create Previews
        createPreviews(preamble, todo);
    }
}

void PreviewThread::createPreviews(const QString& preamble, const std::vector<QString>& todo) {
    qDebug() << "create" << todo;
    binaryCreatePreviews(preamble, todo, 0, todo.size()-1);
}


bool load_pages_from_pdf(QString filename, int expected_number_of_pages, QVector<QImage>& res) {
    std::unique_ptr<Poppler::Document> document(Poppler::Document::load(filename));
    if (!document || document->isLocked()) {
        qDebug() << "Couldn't open PDF file " << filename << ".";
        return false;
    }
    if (document->numPages() != expected_number_of_pages) {
        qDebug() << "Found " << document->numPages() << " pages, expected " << expected_number_of_pages << ".";
        return false;
    }
    document->setRenderHint(Poppler::Document::RenderHint::Antialiasing);
    document->setRenderHint(Poppler::Document::RenderHint::TextAntialiasing);
    document->setRenderHint(Poppler::Document::RenderHint::IgnorePaperColor);
    res.clear();
    res.reserve(expected_number_of_pages);
    for (int pageNumber = 0; pageNumber < expected_number_of_pages; pageNumber++) {
        std::unique_ptr<Poppler::Page> pdfPage(document->page(pageNumber));
        if (!pdfPage) {
            qDebug() << "Couldn't open page " << pageNumber << ".";
            return false;
        }
        QImage img = pdfPage->renderToImage(96, 96);
        if (img.isNull()) {
            qDebug() << "Couldn't render page " << pageNumber << ".";
            return false;
        }
        res.push_back(img);
    }
    return true;
}

void PreviewThread::binaryCreatePreviews(const QString &preamble, const std::vector<QString>& mathenvs, int start, int end) {
    // Check if the preamble changed again in the meantime
    {
        QMutexLocker lock(&m_queue_mutex);
        if (!m_keep_trying)
            return;
    }
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
            // FIXME Double dollar signs do not work! Without the preview environment they do!
            fout << "\n\\begin{preview}\n" << mathenvs[i] << "\n\\end{preview}\n" << endl << endl;
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
    proc.start("pdflatex -interaction=batchmode inpreview.tex");
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
            binaryCreatePreviews(preamble, mathenvs, start, (start+end)/2);
            binaryCreatePreviews(preamble, mathenvs, (start+end)/2+1, end);
        } else {
            qDebug() << "Failed code:" << mathenvs[start];
            std::vector<std::pair<QString,image_state> > upd;
            upd.emplace_back(mathenvs[start], image_error());
            emit picturesAvailable(preamble, upd);
        }
    } else {
        qDebug() << "Succeeded:" << start << "--" << end << "(in" << tim.elapsed()*0.001 << "s)";
        
        std::vector<std::pair<QString,image_state> > upd;
        // Load images from disk
        int ipr = 0;
        for (int i = start; i <= end; i++) {
            image_state img = std::make_shared<QImage>(imgs[ipr]);
            upd.emplace_back(mathenvs[i], img);
            ipr++;
        }
        
        emit picturesAvailable(preamble, upd);
    }
    
    if (keep_folders <= (int)success) {
        qDebug() << "Removing temporary folder" << folder.absolutePath() << ".";
        folder.removeRecursively();
    }
    qDebug();
}
