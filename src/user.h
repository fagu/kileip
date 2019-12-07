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

// Unlike CPart, this doesn't delete its children.
class CollectionPart : public Part {
    public:
        CollectionPart(int st) { start=st; };
        ~CollectionPart() {};
        vector<Part *> children;
        void addChild(Part * p);
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
};

class User;
class ParserResult {
public:
    ParserResult();
    ParserResult(User *user, TextPart *doc, QString text);
    ~ParserResult() {}
    TextPart *doc();
    QString text();
    QList<Part*> mathgroups();
private:
    User *m_user;
    QSharedPointer<TextPart> m_doc;
    QString m_text;
    bool m_initmathgroups;
    QList<Part*> m_mathgroups;
};

// Parser thread
class User : public QThread {
    friend ParserResult;
    Q_OBJECT
    public:
        User();
        ~User();
        // Returns main
        ParserResult data();
        // Notifies the user of a new text
        void textChanged(QString ntext);
    protected:
        void run() override;
    Q_SIGNALS:
        // parsing complete
        void documentChanged();
    private:
        ParserResult m_res;
        
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
        
        static TextPart * document(TextPart* ma, QString text);
        static CollectionPart * preamble(TextPart* ma, QString text);
        /// Returns the mathgroups that are not contained in another mathgroup
        static QList<Part*> getMathgroups(Part *part, QString text);
};

#endif
