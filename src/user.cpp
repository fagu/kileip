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
	Parser p("", 0);
	ParserResult r(ParserResult(this, p.parseText(), ""));
	m_res = r;
}

User::~User() {
	{
		QMutexLocker lock(&mutex);
		abort = true;
		waitcond.wakeOne();
	}
	wait();
	m_res = ParserResult();
}

void User::run() {
	while(true) {
		QString parsetext;
		{
			QMutexLocker lock(&mutex);
			while(m_res.text() == newtext) {
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
		m_res = ParserResult(this, newmain, parsetext);
		emit documentChanged();
	}
}

ParserResult User::data() {
	return m_res;
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


ParserResult::ParserResult() {
	m_user = 0;
	m_initmathgroups = false;
}

ParserResult::ParserResult(User* user, TextPart* doc, QString text) {
	m_user = user;
	m_initmathgroups = false;
	m_doc = doc;
	m_text = text;
	link();
}

ParserResult::ParserResult(const ParserResult& r) {
	m_user = r.m_user;
	m_initmathgroups = false;
	m_doc = r.m_doc;
	m_text = r.m_text;
	link();
}

ParserResult& ParserResult::operator=(const ParserResult& r) {
	unlink();
	m_user = r.m_user;
	m_initmathgroups = false;
	m_doc = r.m_doc;
	m_text = r.m_text;
	link();
	return *this;
}

ParserResult::~ParserResult() {
	unlink();
}

void ParserResult::link() {
	if (!m_user)
		return;
	QMutexLocker lock(&m_user->mutex);
	m_user->mains[m_doc]++;
}

void ParserResult::unlink() {
	if (!m_user)
		return;
	QMutexLocker lock(&m_user->mutex);
	m_user->mains[m_doc]--;
	if (!m_user->mains[m_doc]) {
		m_user->mains.erase(m_user->mains.find(m_doc));
		delete m_doc;
	}
}

TextPart* ParserResult::doc() {
	return m_doc;
}

QString ParserResult::text() {
	return m_text;
}

QList< Part* > ParserResult::mathgroups() {
	if (!m_initmathgroups) {
		Part *pa = m_user->document(m_doc, m_text);
		if (!pa)
			pa = m_doc;
		m_mathgroups = m_user->getMathgroups(pa, m_text);
		m_initmathgroups = true;
	}
	return m_mathgroups;
}

#include "user.moc"
