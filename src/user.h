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

#ifndef USER_H
#define USER_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QList>
#include <QPair>
#include <QSharedPointer>
#include <QStringList>
#include "myparser.h"

class User;
class ParserResult {
public:
    ParserResult(UTextPart&& doc, const QList<PPart>& mathgroups, QString text);
    TextPart *doc();
    QString text();
    QList<PPart> mathgroups();
private:
    UTextPart m_doc;
    QList<PPart> m_mathgroups;
    QString m_text;
};

// Parser thread
class User : public QThread {
friend ParserResult;
Q_OBJECT
public:
    User();
    ~User();
    // Returns main
    std::shared_ptr<ParserResult> data();
    // Notifies the user of a new text
    void textChanged(QString ntext);
protected:
    void run() override;
Q_SIGNALS:
    // parsing complete
    void documentChanged();
private:
    std::shared_ptr<ParserResult> m_res;
    
    // The text to parse next
    QString newtext;
    
    // Mutex for waitcond, m_res, abort, newtext
    QMutex mutex;
    // Wait for new text
    QWaitCondition waitcond;
    // Flag for aborting the thread
    bool abort;
    
public:
    static QStringList mathcommands;
    static QStringList mathbegincommands;
    static QStringList mathenvs;
    static void initMath();
    
    static std::shared_ptr<ParserResult> parse(const QString& text);
    static PTextPart document(PTextPart ma, QString text);
    static Range preamble(PTextPart ma, QString text);
    /// Returns the mathgroups that are not contained in another mathgroup
    static QList<PPart> getMathgroups(PPart part, QString text);
};

#endif
