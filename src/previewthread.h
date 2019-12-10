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

typedef std::pair<int,int> PII;
typedef std::pair<PII,int> PIII;

struct image_error{
    friend bool operator==(image_error, image_error) {
        return true;
    }
    friend bool operator!=(image_error, image_error) {
        return false;
    }
};
struct image_dirty{
    friend bool operator==(image_dirty, image_dirty) {
        return true;
    }
    friend bool operator!=(image_dirty, image_dirty) {
        return false;
    }
};
typedef std::variant<std::shared_ptr<QImage>,image_error,image_dirty> image_state;
typedef std::vector<std::pair<QString,image_state> > VPSI;
Q_DECLARE_METATYPE(VPSI);

class PreviewThread : public QThread {
Q_OBJECT

public:
    PreviewThread(KileDocument::LaTeXInfo *info, QObject *parent = 0);
    ~PreviewThread();
    
    void run() override;
    
    void setDoc(KTextEditor::Document *doc);
    
    void setPreamble(const QString& str);
    
    void enqueue(const std::vector<QString>& maths);
private:
    bool m_abort; // whether to abort the thread
    
    KTextEditor::Document *m_doc;
    KileDocument::LaTeXInfo* m_info;
    
    QString m_preamble;
    std::queue<QString> m_queue;
    QMutex m_queue_mutex;
    QWaitCondition m_queue_wait;
    
    // The temporary directory containing directories numbered 1, 2, 3, ... (one for each run of latex).
    std::unique_ptr<QTemporaryDir> m_dir;
    
    int m_currentrun;
    
    bool m_keep_trying;
    
    void createPreviews(const QString& preamble, const std::vector<QString>& todo);
    void binaryCreatePreviews(const QString &preamble, const std::vector<QString>& mathenvs, int start, int end);
Q_SIGNALS:
    void picturesAvailable(QString,VPSI);
};

#endif
