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

#include "user.h"
#include <QDebug>
#include <QFile>
#include <KGlobal>
#include <KStandardDirs>
#include <QTime>

QStringList User::mathbegincommands;
QStringList User::mathcommands;
QStringList User::mathenvs;

void User::initMath() {
	if (mathenvs.size())
		return;
	QString commandsfilename = KGlobal::dirs()->findResource("appdata", "parser/maths.txt");
	QFile comfile(commandsfilename);
	if (!comfile.exists())
		qDebug() << "maths.txt missing";
	comfile.open(QIODevice::ReadOnly | QIODevice::Text);

	QTextStream in(&comfile);
	in.setCodec("UTF-8");
	int state = 0;
	while (!in.atEnd()) {
		QString line = in.readLine();
		if (line.at(0) == '#')
			continue;
		if (state == 0) {
			if (line == "!begincommand")
				state = 1;
			else {
				mathenvs << line;
			}
		} else if (state == 1) {
			if (line == "!command")
				state = 2;
			else
				mathbegincommands << line;
		} else {
			mathcommands << line;
		}
	}
	comfile.close();
}

TextPart * User::document(TextPart *ma, QString text) {
	for (unsigned int i = 0; i < ma->children.size(); i++) {
		EnvironmentPart *ep = dynamic_cast<EnvironmentPart*>(ma->children[i]);
		if (ep) {
			if (ep->begin->name(text) == "\\begin" && dynamic_cast<CommandWithArgsPart*>(ep->begin)->children.size()) {
				if (dynamic_cast<TextPart*>(dynamic_cast<CommandWithArgsPart*>(ep->begin)->children[0])->sourceWithoutVoid(text) == "document") {
					return ep->body;
				}
			}
		}
	}
	return 0;
}

CollectionPart * User::preamble(TextPart *ma, QString text) {
	int preend = text.length()-1;
	for (unsigned int i = 0; i < ma->children.size(); i++) {
		EnvironmentPart *ep = dynamic_cast<EnvironmentPart*>(ma->children[i]);
		if (ep) {
			if (ep->begin->name(text) == "\\begin" && dynamic_cast<CommandWithArgsPart*>(ep->begin)->children.size()) {
				if (dynamic_cast<TextPart*>(dynamic_cast<CommandWithArgsPart*>(ep->begin)->children[0])->sourceWithoutVoid(text) == "document") {
					preend = ep->start-1;
				}
			}
		}
	}
	
	CollectionPart * preamble = new CollectionPart(0);
	preamble->end = preend;
	for (unsigned int i = 0; i < ma->children.size(); i++) {
		if (ma->children[i]->end <= preend)
			preamble->addChild(ma->children[i]);
		else
			break;
	}
	return preamble;
}

QList< Part* > User::getMathgroups ( Part* part, QString text ) {
	initMath();
	QList<Part*> res;
	EnvironmentPart *ep = dynamic_cast<EnvironmentPart*>(part);
	if (ep) {
		CommandPart * begin = ep->begin;
		QString name = begin->name(text);
		bool gef = false;
		if (mathbegincommands.contains(name))
			gef = true;
		else if (name == "\\begin") {
			CommandWithArgsPart *wa = dynamic_cast<CommandWithArgsPart*>(begin);
			if (wa->children.size()) {
				QString envname = dynamic_cast<TextPart*>(wa->children[0])->sourceWithoutVoid(text);
				if (mathenvs.contains(envname))
					gef = true;
			}
		}
		if (gef) {
			res += ep;
		} else {
			res += getMathgroups(ep->begin, text);
			res += getMathgroups(ep->body, text);
			res += getMathgroups(ep->ending, text);
		}
	}
	CPart *cp = dynamic_cast<CPart*>(part);
	if (cp) {
		CommandPart *cop = dynamic_cast<CommandPart*>(cp);
		if (cop) {
			if (mathcommands.contains(cop->name(text))) {
				res += cop;
				return res;
			}
			CommandWithArgsPart *cwp = dynamic_cast<CommandWithArgsPart*>(cp);
			if (cwp) {
				res += getMathgroups(cwp->optional, text);
			}
		}
		for (unsigned int i = 0; i < cp->children.size(); i++) {
			res += getMathgroups(cp->children[i], text);
		}
	}
	return res;
}

User::User() {
	abort = false;
	text = "";
	Parser p("", 0);
	m_main = p.parseText();
	mains.append(QPair<TextPart*,int>(m_main, 1));
}

User::~User() {
	{
		QMutexLocker lock(&mutex);
		abort = true;
		waitcond.wakeOne();
	}
	wait();
	finished(m_main);
}

void User::run() {
	while(true) {
		QString parsetext;
		{
			QMutexLocker lock(&mutex);
			while(text == newtext) {
				waitcond.wait(&mutex);
				if (abort)
					return;
			}
			parsetext = newtext;
		}
		qDebug() << "parsen...";
		Parser p(parsetext, 0);
		qDebug() << "fertig\n";
		//QTime tim; tim.start();
		TextPart *newmain = p.parseText();
		//qDebug() << "Parsing time:" << tim.elapsed()*0.001;
		TextPart *oldmain;
		{
			QMutexLocker lock(&mutex);
			oldmain = m_main;
			m_main = newmain;
			text = parsetext;
			mains.append(QPair<TextPart*,int>(m_main, 1));
		}
		finished(oldmain);
		emit documentChanged();
	}
}

QPair<TextPart*,QString> User::data() {
	QMutexLocker lock(&mutex);
	mains.last().second++;
	return qMakePair(m_main, text);
}

void User::finished(TextPart *main) {
	QMutexLocker lock(&mutex);
	for (int i = 0; i < mains.size(); i++) {
		if (mains[i].first == main) {
			mains[i].second--;
			if (mains[i].second == 0) {
				mains.removeAt(i);
				delete main;
			}
			break;
		}
	}
}


void User::textChanged(QString ntext) {
	QMutexLocker lock(&mutex);
	newtext = ntext;
	waitcond.wakeOne();
}



void CollectionPart::addChild ( Part* p ) {
	children.push_back(p);
}

QString CollectionPart::toString ( const QString& text ) const {
	QString st = "(collection: ";
	for (unsigned int i = 0; i < children.size(); i++) {
		if (i > 0)
			st += ";";
		st += children[i]->toString(text);
	}
	st += ")";
	return st;
}


QString CollectionPart::toTeX ( const QString& text ) const {
	QString st = " ";
	for (unsigned int i = 0; i < children.size(); i++) {
		st += children[i]->toTeX(text);
	}
	return st + " ";
}



#include "user.moc"
