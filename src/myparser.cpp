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

// TODO strange paragraph handling: "merry%\n\nchristmas" makes a new paragraph, but "merry%\nchristmas" doesn't make a space

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <KGlobal>
#include <KStandardDirs>
#include "myparser.h"

bool midEqual(const QString &text, int start, int len, const QString &ref) {
	if (len != ref.length())
		return false;
	for (int a = 0; a < ref.length(); a++)
		if (text.at(start+a) != ref.at(a))
			return false;
	return true;
}

QString Part::source(const QString &text) const{
	return text.mid(start, end-start+1);
}

CPart::~CPart() {
	for (unsigned int i = 0; i < children.size(); i++)
		delete children[i];
}

void CPart::addChild(Part * p) {
	children.push_back(p);
}

EnvironmentPart::~EnvironmentPart() {
	delete begin;
	delete ending;
	delete body;
}

CommentPart::CommentPart(int s) {
	start = s;
}

QString CommentPart::toString(const QString &text) const {
	QString st = "(comment: ";
	st += text.mid(start, end-start+1);
	st += ")";
	return st;
}

QString CommentPart::toTeX(const QString &text) const {
	return text.mid(start, end-start+1);
}

TextPart::TextPart(int s) {
	start = s;
}

QString TextPart::toString(const QString &text) const {
	QString st;// = "(";
	int s = start;
	for (unsigned int i = 0; i < children.size(); i++) {
		st += text.mid(s, children[i]->start-s);
		st += children[i]->toString(text);
		s = children[i]->end+1;
	}
	st += text.mid(s, end-s+1);
	//st += ")";
	return st;
}

QString TextPart::toTeX(const QString &text) const {
	QString st;
	int s = start;
	for (unsigned int i = 0; i < children.size(); i++) {
		st += text.mid(s, children[i]->start-s);
		st += children[i]->toTeX(text);
		s = children[i]->end+1;
	}
	st += text.mid(s, end-s+1);
	return st;
}

QString TextPart::sourceWithoutVoid(const QString &text) const {
	if (children.size() == 1) {
		EnvironmentPart *ep = dynamic_cast<EnvironmentPart*>(children[0]);
		if (ep)
			if (ep->begin->name(text) == "{")
				return ep->body->source(text);
	}
	return source(text);
}

CommandWithArgsPart::CommandWithArgsPart(int s) {
	start = s;
	nameend = -1;
	optional = 0;
}

CommandWithArgsPart::~CommandWithArgsPart() {
	delete optional;
}

QString CommandWithArgsPart::name(const QString &text) const {
	return text.mid(start, nameend-start+1);
}

bool CommandWithArgsPart::nameEq(const QString &text, const QString &ref) const {
	return midEqual(text, start, nameend-start+1, ref);
}

int CommandWithArgsPart::numArgs(const QString &text) const {
	QString na = name(text);
	if (Global::commands.contains(na))
		return Global::commands[na];
	else if (na == "\\begin") {
		if (children.size()) {
			QString envname = "E" + dynamic_cast<TextPart*>(children[0])->sourceWithoutVoid(text);
			if (Global::commands.contains(envname))
				return Global::commands[envname]+1;
			else
				return -1;
		} else
			return 1;
	}
	else
		return -1;
}

int CommandWithArgsPart::remainingArgs(const QString &text) const {
	return numArgs(text) - children.size();
}

QString CommandWithArgsPart::toString(const QString &text) const {
	QString st = "(command: ";
	st += text.mid(start, nameend-start+1);
	if (optional || children.size())
		st += ":";
	if (optional)
		st += optional->toString(text);
	for (unsigned int i = 0; i < children.size(); i++) {
		if (i > 0 || optional)
			st += ";";
		st += children[i]->toString(text);
	}
	st += ")";
	return st;
}

QString CommandWithArgsPart::toTeX(const QString &text) const {
	QString st = text.mid(start, nameend-start+1);
	st += " ";
	if (optional)
		st += optional->toTeX(text);
	for (unsigned int i = 0; i < children.size(); i++) {
		st += children[i]->toTeX(text);
	}
	return st + " ";
}


QString PrimitiveCommandPart::name ( const QString& text ) const {
	return text.mid(start, end-start+1);
}

bool PrimitiveCommandPart::nameEq(const QString &text, const QString &ref) const {
	return midEqual(text, start, end-start+1, ref);
}

QString PrimitiveCommandPart::toString ( const QString& text ) const {
	return text.mid(start, end-start+1);
}

QString PrimitiveCommandPart::toTeX ( const QString& text ) const {
	return text.mid(start, end-start+1);
}





EnvironmentPart::EnvironmentPart(CommandPart * cp) {
	begin = cp;
	start = cp->start;
	body = 0;
	ending = 0;
}

QString EnvironmentPart::toString(const QString &text) const {
	QString st = "(env: ";
	st += begin->toString(text);
	st += ";";
	st += body->toString(text);
	st += ";";
	if (ending)
		st += ending->toString(text);
	else
		st += "MISSING";
	st += ")";
	return st;
}

QString EnvironmentPart::toTeX(const QString &text) const {
	QString st = begin->toTeX(text);
	st += " ";
	st += body->toTeX(text);
	if (ending)
		st += " " + ending->toString(text);
	return st + " ";
}



Ending::Ending(QString w) {
	wanted = w;
	cp = 0;
	//found = -1;
}


Parser::Parser(QString ptext, int pstart) : text(ptext),i(pstart) {
	Global::init();
}

TextPart * Parser::parseText() {
	textdata = text.constData();
	endings.push_back(Ending(QString("")));
	return parseText(0);
}


TextPart * Parser::parseText(int parsefrom) {
	TextPart * tp = new TextPart(i);
	i = parsefrom;
	bool lastnewline = false;
	while(true) {
		//if (endings[endings.size()-1].found != -1) {
		//	tp->end = endings[endings.size()-1].found-1;
		//	break;
		//} 
		if (i >= text.length()) {
			tp->end = i-1;
			break;
		} else {
			if (textdata[i] == '\n') {
				if (lastnewline) {
					while(i > 0 && textdata[i-1].isSpace())
						i--;
					while(textdata[i] != '\n')
						i++;
					CommandPart *cp = new PrimitiveCommandPart(i);
					while(i < text.length()-1 && textdata[i+1].isSpace())
						i++;
					while(textdata[i] != '\n')
						i--;
					cp->end = i;
					if (endingFound(cp)) {
						i++;
						tp->end = cp->start-1;
						break;
					} else {
						tp->addChild(cp);
					}
				} else {
					lastnewline = true;
				}
			} else if (!textdata[i].isSpace()) {
				lastnewline = false;
			}
			if (textdata[i] == '%') {
				//qDebug() << "comment";
				CommentPart * cp = parseComment(); // TODO include Comment in parsed document
				tp->addChild(cp);
				//delete cp;
			} else if (textdata[i] == '\\') {
				CommandWithArgsPart * cp = parseCommand();
				QString commandname = cp->name(text);
				if (endingFound(cp)) {
					tp->end = cp->start-1;
					break;
				} else if ((commandname == "\\begin" && cp->children.size()) || Global::specialenvs.contains(commandname)) {
					EnvironmentPart * ep = parseEnvironment(cp);
					tp->addChild(ep);
				} else {
					tp->addChild(cp);
				}
			} else if (textdata[i] == '{') {
				//CommandPart * cp = new CommandPart(i);
				//cp->nameend = i;
				//cp->end = i;
				//i++;
				CommandPart * cp = parsePrimitiveCommand();
				EnvironmentPart * ep = parseEnvironment(cp);
				tp->addChild(ep);
			} else if (textdata[i] == '}' || textdata[i] == ']') {
				//CommandPart * cp = new CommandPart(i);
				//cp->nameend = i;
				//cp->end = i;
				//i++;
				CommandPart * cp = parsePrimitiveCommand();
				if (endingFound(cp)) {
					tp->end = cp->start-1;
					break;
				} else
					delete cp;
			} else if (textdata[i] == '$') {
				CommandPart * cp = parsePrimitiveCommand();
				if (expectedEndingFound(cp)) {
					tp->end = cp->start-1;
					break;
				} else {
					EnvironmentPart * ep = parseEnvironment(cp);
					tp->addChild(ep);
				}
			} else {
				i++;
			}
		}
		if (endings.size() && endings[endings.size()-1].wanted == "1") {
			tp->end = i-1;
			break;
		}
	}
	return tp;
}

CommentPart * Parser::parseComment() {
	CommentPart * cp = new CommentPart(i);
	while(true) {
		if (i >= text.length()) {
			cp->end = i-1;
			break;
		} else if (textdata[i] == '\n') {
			cp->end = i;
			i++;
			break;
		}
		i++;
	}
	return cp;
}

CommandWithArgsPart * Parser::parseCommand() {
	CommandWithArgsPart * cp = new CommandWithArgsPart(i);
	i++;
	bool lastnewline = false;
	while(true) {
		if (i >= text.length()) {
			if (cp->nameend == -1)
				cp->nameend = i-1;
			while(i > 0 && textdata[i-1].isSpace())
				i--;
			cp->end = i-1;
			break;
		} else {
			if (cp->nameend == -1) {
				if (code(textdata[i]) != 11) {
					if (i != cp->start+1) {
						cp->nameend = i-1;
					} else {
						cp->nameend = i;
						i++;
					}
				} else {
					i++;
				}
			} else {
				if (!textdata[i].isSpace()) {
					lastnewline = false;
					if (textdata[i] == '%') {
						CommentPart * comment = parseComment();
						delete comment; // TODO Handle comments
					} else if (textdata[i] == '}') {
						cp->end = i-1;
						break;
					} else if (textdata[i] == '[') {
						//CommandPart * ccp = new CommandPart(i);
						//ccp->nameend = i;
						//ccp->end = i;
						//i++;
						PrimitiveCommandPart * ccp = parsePrimitiveCommand();
						EnvironmentPart * arg = parseEnvironment(ccp);
						cp->optional = arg;
					} else if (cp->remainingArgs(text) > 0 || (cp->numArgs(text) == -1 && textdata[i] == '{')) {
						endings.push_back(Ending(QString("1")));
						TextPart * arg = parseText(i);
						endings.pop_back();
						cp->addChild(arg);
					} else {
						while(i > 0 && textdata[i-1].isSpace())
							i--;
						cp->end = i-1;
						break;
					}
				} else {
					if (textdata[i] == '\n') {
						if (lastnewline) {
							i--;
							while(i > 0 && textdata[i-1].isSpace())
								i--;
							cp->end = i-1;
							break;
						} else {
							lastnewline = true;
						}
					}
					i++;
				}
			}
		}
	}
	return cp;
}

PrimitiveCommandPart* Parser::parsePrimitiveCommand() {
	PrimitiveCommandPart * cp = new PrimitiveCommandPart(i);
	if (textdata[i] == '$') {
		i++;
		if (i < text.length() && textdata[i] == '$') {
			cp->end = i;
			i++;
		} else {
			cp->end = i-1;
		}
	} else {
		i++;
		cp->end = i-1;
	}
	return cp;
}


EnvironmentPart * Parser::parseEnvironment(CommandPart * begincommand) {
	EnvironmentPart * ep = new EnvironmentPart(begincommand);
	QString commandname = begincommand->name(text);
	QString wanted;
	bool epoe = true;
	if (commandname == "{")
		wanted = "}";
	else if (commandname == "[")
		wanted = "]";
	else if (commandname == "\\begin")
		wanted = "E" + dynamic_cast<CommandWithArgsPart*>(begincommand)->children[0]->source(text);
	else if (Global::specialenvs.contains(commandname)) {
		wanted = Global::specialenvs[commandname].endcommand;
		epoe = Global::specialenvs[commandname].partofenvironment;
	}
	endings.push_back(Ending(wanted));
	ep->body = parseText(i);
	Ending ne = endings[endings.size()-1];
	if (wanted == "$" && ne.cp && text[ne.cp->start] == '\n')
		epoe = false;
	if (epoe) {
		ep->ending = ne.cp;
		if (ne.cp)
			ep->end = ne.cp->end;
		else {
			ep->end = ep->body->end;
			i = ep->end+1;
		}
	} else {
		if (ne.cp)
			delete ne.cp;
		ep->end = ep->body->end;
		i = ep->end+1;
	}
	endings.pop_back();
	return ep;
}

bool Parser::endingFound(CommandPart * cp) {
	if (matches(endings[endings.size()-1].wanted, cp)) {
		endings[endings.size()-1].cp = cp;
		return true;
	}
	for (int k = endings.size()-2; k >= 0; k--) {
		if (matches(endings[k].wanted, cp)) {
			//endings[k].cp = cp;
			//for (unsigned int l = k; l < endings.size(); l++)
			//	endings[l].found = cp->start;
			i = cp->start;
			return true;
		}
	}
	return false;
}

bool Parser::expectedEndingFound(CommandPart * cp) {
	if (matches(endings[endings.size()-1].wanted, cp)) {
		endings[endings.size()-1].cp = cp;
		return true;
	}
	return false;
}

bool Parser::matches(QString wanted, CommandPart * cp) {
	if (wanted.length() >= 1 && wanted.at(0) == 'E') {
		CommandWithArgsPart *cwp = dynamic_cast<CommandWithArgsPart*>(cp);
		if (!cwp)
			return false;
		return cp->nameEq(text, QString("\\end")) && cwp->children.size() && "E" + cwp->children[0]->source(text) == wanted;
	} else {
		return cp->nameEq(text, wanted) || (wanted == "$" && text[cp->start] == '\n');
	}
}


int Parser::code(QChar c) {
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return 11;
	return -1; // TODO Return letter-codes as in the TeXbook
}


QMap<QString,int> Global::commands;
QMap<QString,SpecialEnvironment> Global::specialenvs;

void Global::init() {
	if (!commands.empty())
		return;
	QString commandsfilename = KGlobal::dirs()->findResource("appdata", "parser/commands.txt");
	//QString commandsfilename = "commands.txt";
	QFile comfile(commandsfilename);
	if (!comfile.exists())
		qDebug() << "commands.txt missing";
	comfile.open(QIODevice::ReadOnly | QIODevice::Text);

	//QString text;
	QTextStream in(&comfile);
	in.setCodec("UTF-8");
	int state = 0;
	while (!in.atEnd()) {
		QString line = in.readLine();
		if (line.at(0) == '#')
			continue;
		if (state == 0) { // Read number of arguments for commands
			if (line == "!envs")
				state = 1;
			else {
				int space = line.indexOf(' ');
				int numargs = line.right(line.length()-space-1).replace(" ","").toInt();
				commands[line.left(space)] = numargs;
			}
		} else if (state == 1) { // Read special environments
			QStringList toks = line.split(QRegExp("\\s+"));
			SpecialEnvironment se;
			se.endcommand = toks[1];
			se.partofenvironment = (toks[2].toInt() != 0);
			specialenvs[toks[0]] = se;
		}
	}
	comfile.close();
}



