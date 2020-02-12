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
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <ktexteditor/document.h>
#include <set>
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
    // Kill currently running latex process and stop the thread.
    ~PreviewThread();
    
    void run() override;
    
    // Sets the preamble for compiling the document. This clears the queue and stops generating earlier pictures.
    void setPreamble(const QString& str);
    
    // Adds the given formulas to the queue.
    void enqueue(const std::vector<QString>& maths);
    // Removes the given formulas from the queue, if they are present.
    void remove_from_queue(const std::vector<QString>& maths);
private:
    // Abort the thread.
    bool m_abort;
    // Abort the current run of createPreviews().
    bool m_abort_current;
    
    KileDocument::LaTeXInfo* m_info;
    
    qreal m_dpix, m_dpiy;
    
    QString m_preamble;
    std::set<QString> m_queue;
    QMutex m_queue_mutex;
    QWaitCondition m_queue_wait;
    
    // Currently running latex process.
    QProcess* m_process = nullptr;
    
    // The temporary directory containing directories numbered 1, 2, 3, ... (one for each run of latex).
    QTemporaryDir m_dir;
    
    // Number of the current run of latex. (Used as the temporary directory's name.)
    int m_currentrun;
    
    bool load_pages_from_pdf(QString filename, int expected_number_of_pages, QVector<QImage>& res);
    void createPreviews(const QString& preamble, const std::vector<QString>& todo);
    void binaryCreatePreviews(const QString &preamble, const std::vector<QString>& mathenvs, int start, int end);
Q_SIGNALS:
    void picturesAvailable(QString,VPSI);
};

#endif
