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

class User : public QThread {
	Q_OBJECT
	public:
		User();
		~User();
		/// Returs main
		QPair< TextPart*, QString > data();
		QString dataText();
		void textChanged(QString ntext);
		void finished(TextPart * main);
	protected:
		void run();
	signals:
		void documentChanged();
	private:
		QString text;
		TextPart * m_main;
		
		QString newtext;
		
		QMutex mutex;
		QWaitCondition waitcond;
		bool abort;
		
		QList<QPair<TextPart*,int> > mains;
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
