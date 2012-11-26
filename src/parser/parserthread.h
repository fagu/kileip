/**************************************************************************
*   Copyright (C) 2011-2012 by Michel Ludwig (michel.ludwig@kdemail.net)  *
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PARSERTHREAD_H
#define PARSERTHREAD_H

#include <QMutex>
#include <QPair>
#include <QThread>
#include <QQueue>
#include <QWaitCondition>

#include <KUrl>

#include "documentinfo.h"

#include "parser.h"

class KileInfo;

namespace KileDocument {
	class Info;
	class TextInfo;
}

namespace KileParser {

class Parser;
class ParserInput;
class ParserOutput;

enum ParserType { LaTeX = 0, BibTeX };

// NOTE: we cannot store pointer to TextInfo objects in the queue
//       as this would cause too many problems when they are deleted
//       and their content is still being parsed
class DocumentParserInput : public ParserInput
{
public:
	DocumentParserInput(const KUrl& url, QStringList lines,
	                                     ParserType parserType,
	                                     const QMap<QString, KileStructData>* dictStructLevel,
	                                     bool showSectioningLabels,
	                                     bool showStructureTodo);

	QStringList lines;
	ParserType parserType;
	const QMap<QString, KileStructData>* dictStructLevel;
	bool showSectioningLabels;
	bool showStructureTodo;
};

class ParserThread : public QThread
{
	Q_OBJECT

public:
	ParserThread(KileInfo *info, QObject *parent = 0);
	virtual ~ParserThread();

	void stopParsing();

	bool shouldContinueDocumentParsing();

	bool isParsingComplete();

Q_SIGNALS:
	/**
	 * The ownership of the 'output' object is transferred to the slot(s)
	 * connected to this signal.
	 **/
	void parsingComplete(const KUrl& url, KileParser::ParserOutput* output);

	void parsingQueueEmpty();
	void parsingStarted();

protected:
	KileInfo *m_ki;

	void addParserInput(ParserInput *input);
	void removeParserInput(const KUrl& url);

	void run();

	virtual Parser* createParser(ParserInput *input) = 0;

private:
	bool m_keepParserThreadAlive;
	bool m_keepParsingDocument;
	QQueue<ParserInput*> m_parserQueue;
	KUrl m_currentlyParsedUrl;
	QMutex m_parserMutex;
	QWaitCondition m_queueEmptyWaitCondition;
};


class DocumentParserThread : public ParserThread
{
	Q_OBJECT

public:
	DocumentParserThread(KileInfo *info, QObject *parent = NULL);
	virtual ~DocumentParserThread();

public Q_SLOTS:
	void addDocument(KileDocument::TextInfo *textInfo);
	void removeDocument(KileDocument::TextInfo *textInfo);
	void removeDocument(const KUrl& url);

protected:
	virtual Parser* createParser(ParserInput *input);

};


class OutputParserThread: public ParserThread
{
	Q_OBJECT

public:
	OutputParserThread(KileInfo *info, QObject *parent = NULL);
	virtual ~OutputParserThread();

public Q_SLOTS:
	void addLaTeXLogFile(const QString& logFile, const QString& sourceFile,
	                     // for QuickPreview
	                     const QString& texFileName = "", int selrow = -1, int docrow = -1);
	void removeFile(const QString& fileName);

protected:
	virtual Parser* createParser(ParserInput *input);
};

}

#endif
