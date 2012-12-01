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
#include <QStringList>
#include "myparser.h"

class CollectionPart : public Part {
	public:
		CollectionPart(int st) { start=st; };
		~CollectionPart() {};
		vector<Part *> children;
		void addChild(Part * p);
		QString toString(const QString &text) const;
		QString toTeX(const QString &text) const;
};

class User;
class ParserResult {
public:
	ParserResult();
	ParserResult(User *user, TextPart *doc, QString text);
	ParserResult(const ParserResult &r);
	ParserResult& operator=(const ParserResult &r);
	~ParserResult();
	TextPart *doc();
	QString text();
	QList<Part*> mathgroups();
private:
	User *m_user;
	TextPart *m_doc;
	QString m_text;
	bool m_initmathgroups;
	QList<Part*> m_mathgroups;
};

class User : public QThread {
	friend ParserResult;
	Q_OBJECT
	public:
		User();
		~User();
		/// Returs main
		ParserResult data();
		void textChanged(QString ntext);
	protected:
		void run();
	signals:
		void documentChanged();
	private:
		ParserResult m_res;
		
		QString newtext;
		
		QMutex mutex;
		QWaitCondition waitcond;
		bool abort;
		
		QMap<TextPart*,int> mains;
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
