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

#ifndef MYPARSER_H
#define MYPARSER_H

#include <QString>
#include <QMap>
#include <QSharedDataPointer>
#include <stack>
#include <vector>

using namespace std;

class TextPart;

bool midEqual(const QString &text, int start, int len, const QString &ref);

class Part : public QSharedData {
    public:
        int start, end;
        virtual ~Part() {};
        virtual QString toString(const QString &text) const=0;
        virtual QString toTeX(const QString &text) const=0;
        QString source(const QString &text) const;
        //virtual int visit();
};

//typedef QExplicitlySharedDataPointer<Part> PPart;
class TextPart;
//typedef QExplicitlySharedDataPointer<TextPart> PTextPart;

class CPart {
    public:
        vector<Part *> children;
        virtual ~CPart();
        void addChild(Part * p);
        //int visit();
};

class CommentPart : public Part {
    public:
        CommentPart(int start);
        ~CommentPart() {};
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
};

class CommandPart : public Part {
    public:
        virtual QString name(const QString &text) const = 0;
        virtual bool nameEq(const QString &text, const QString &ref) const = 0;
};

class CommandWithArgsPart : public CommandPart, public CPart {
    public:
        int nameend;
        Part * optional;
        CommandWithArgsPart(int start);
        ~CommandWithArgsPart();
        QString name(const QString &text) const override;
        bool nameEq(const QString &text, const QString &ref) const override;
        int numArgs(const QString &text) const;
        int remainingArgs(const QString &text) const;
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
};

class PrimitiveCommandPart : public CommandPart {
    public:
        PrimitiveCommandPart(int st) { start = st; };
        ~PrimitiveCommandPart() {};
        QString name(const QString &text) const override;
        bool nameEq(const QString &text, const QString &ref) const override;
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
};

class TextPart : public CPart, public Part {
    public:
        TextPart(int start);
//         virtual ~TextPart() {};
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
        QString sourceWithoutVoid(const QString &text) const;
};

//typedef QExplicitlySharedDataPointer<CommandPart> PCommandPart;
//typedef QExplicitlySharedDataPointer<CommentPart> PCommentPart;

class EnvironmentPart : public Part {
    public:
        CommandPart * begin;
        CommandPart * ending;
        TextPart * body;
        EnvironmentPart(CommandPart * cp);
        ~EnvironmentPart();
        QString toString(const QString &text) const override;
        QString toTeX(const QString &text) const override;
        //int visit();
};

//typedef QExplicitlySharedDataPointer<EnvironmentPart> PEnvironmentPart;


class Ending {
    public:
        QString wanted;
        //int found;
        CommandPart * cp;
        Ending(QString w);
        // The behavior appears to be undefined unless we explicitly request these default constructors.
        Ending(const Ending&) = default;
        Ending& operator=(const Ending&) = default;
};

class Parser {
    private:
        QString text;
        const QChar *textdata;
        int i;
        vector<Ending> endings;
        int code(QChar c);
    
    public:
        Parser(QString ptext, int pstart);
        TextPart * parse();
    private:
        TextPart * parseText();
        CommentPart * parseComment();
        CommandWithArgsPart * parseCommand();
        PrimitiveCommandPart * parsePrimitiveCommand();
        EnvironmentPart * parseEnvironment(CommandPart * begincommand);
        bool endingFound(CommandPart * cp);
        bool expectedEndingFound(CommandPart *cp);
        bool matches(QString wanted, CommandPart * cp);
};

struct SpecialEnvironment {
    QString endcommand;
    bool partofenvironment;
};

class Global {
    public:
        static QMap<QString,int> commands;
        static QMap<QString,SpecialEnvironment> specialenvs;
        static void init();
};

#endif
