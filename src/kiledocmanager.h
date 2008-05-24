//
// C++ Interface: kiledocmanager
//
// Description: 
//
//
// Author: Jeroen Wijnhout <Jeroen.Wijnhout@kdemail.net>, (C) 2004
//         Michel Ludwig <michel.ludwig@kdemail.net>, (C) 2006-2008
//
// Copyright: See COPYING file that comes with this distribution
//

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
 
#ifndef KILEDOCUMENTKILEDOCMANAGER_H
#define KILEDOCUMENTKILEDOCMANAGER_H

#include <QDropEvent>
#include <QObject>
#include <QStringList>

#include <KTextEditor/Editor>
#include <KTextEditor/ModificationInterface>

#include "kileconstants.h"
#include "kileproject.h"

class KUrl;
class KFileItem;
class KProgressDialog;
namespace KTextEditor {class Document; class View;}

class TemplateItem;
class KileInfo;
class KileProjectItem;

namespace KileDocument 
{

class Info;
class TextInfo;

class Manager : public QObject
{
	Q_OBJECT
public:
	Manager(KileInfo *info, QObject *parent = NULL, const char *name = NULL);
	~Manager();

public Q_SLOTS:
	KTextEditor::View* createNewJScript();
	KTextEditor::View* createNewLaTeXDocument();

//files
	void newDocumentStatus(KTextEditor::Document *doc);

	/**
	 * Creates a new file on disk.
	 **/
	void fileNew(const KUrl&);
	void fileNew();

	void fileSelected(const KUrl&);
	void fileSelected(const KileProjectItem *item);
	void fileSelected(const KFileItem& file);

	void fileOpen();
	void fileOpen(const KUrl& url, const QString& encoding = QString(), int index = -1);

	void fileSave();
	void fileSaveAs(KTextEditor::View* = NULL);
	void fileSaveCopyAs();

	void saveURL(const KUrl&);
	void fileSaveAll(bool amAutoSaving = false, bool disUntitled = false);

	bool fileCloseAllOthers();
	bool fileCloseAll();
	bool fileClose(const KUrl& url);
	bool fileClose(KTextEditor::Document *doc = NULL, bool closingproject = false);

//templates
	KTextEditor::View* loadTemplate(TemplateItem*);
	void createTemplate();
	void removeTemplate();
	void replaceTemplateVariables(QString &line);

//projects
	void projectNew();
	KileProject* projectOpen();
	
	/**
	 * @param openProjectItemViews Opens project files in the editor iff openProjectItemViews is set to 'true'.
	 **/
	KileProject* projectOpen(const KUrl&, int = 0, int = 1, bool openProjectItemViews = true);

	/**
	 * @param openProjectItemViews Opens project files in the editor iff openProjectItemViews is set to 'true'.
	 **/
	void projectOpenItem(KileProjectItem *item, bool openProjectItemViews = true);

	/**
	 * Saves the state of the project, if @param project is zero, the active project is saved.
	 **/
	void projectSave(KileProject* project = NULL);
	void projectAddFiles(const KUrl&);
	void projectAddFiles(KileProject* project = NULL,const KUrl & url = KUrl());
	void toggleArchive(KileProjectItem *);
	void buildProjectTree(KileProject *project = NULL);
	void buildProjectTree(const KUrl&);
	void projectOptions(const KUrl&);
	void projectOptions(KileProject *project = NULL);
	bool projectClose(const KUrl& url = KUrl());
	bool projectCloseAll();

	void projectShow();
	void projectRemoveFiles();
	void projectShowFiles();
	void projectAddFile(QString filename, bool graphics=false);
	void projectOpenAllFiles();
	void projectOpenAllFiles(const KUrl&);

	KileProject* selectProject(const QString&);

	void addProject(KileProject *project);
	void addToProject(const KUrl&);
	void addToProject(KileProject*, const KUrl&);
	void removeFromProject(KileProjectItem *item);
	void storeProjectItem(KileProjectItem *item, KTextEditor::Document *doc);

	void cleanUpTempFiles(const KUrl &url, bool silent = false);

	void openDroppedURLs(QDropEvent *e);

Q_SIGNALS:
	void projectTreeChanged(const KileProject*);
	void closingDocument(KileDocument::Info*);
	void documentInfoCreated(KileDocument::Info*);

	void updateStructure(bool needToParse, KileDocument::Info*);
	void updateModeStatus();
	void updateReferences(KileDocument::Info*);

	void documentModificationStatusChanged(KTextEditor::Document*, bool, KTextEditor::ModificationInterface::ModifiedOnDiskReason);
	void documentUrlChanged(KTextEditor::Document*);
	void documentNameChanged(KTextEditor::Document*);

	void addToRecentFiles(const KUrl&);
	void addToRecentProjects(const KUrl&);
	void removeFromRecentProjects(const KUrl&);

	void startWizard();

	void removeFromProjectView(const KUrl&);
	void removeFromProjectView(const KileProject*);
	void removeItemFromProjectView(const KileProjectItem*, bool);
	void addToProjectView(const KUrl&);
	void addToProjectView(KileProjectItem *item);
	void addToProjectView(const KileProject*);

public:
	KTextEditor::Editor* getEditor();

	QList<KileProject*> projects() { return m_projects; }
	QList<TextInfo*> textDocumentInfos() { return m_textInfoList; }

	KTextEditor::Document* docFor(const KUrl &url);

	Info* getInfo() const;
	// FIXME: "path" should be changed to a URL, i.e. only the next but one function 
	//        should be used
	TextInfo* textInfoFor(const QString &path) const;
	TextInfo* textInfoForURL(const KUrl& url);
	TextInfo* textInfoFor(KTextEditor::Document* doc) const;
	void updateInfos();

	KileProject* projectFor(const KUrl &projecturl);
	KileProject* projectFor(const QString & name);
	KileProject* activeProject();
	bool isProjectOpen();
	void updateProjectReferences(KileProject *project);
	QStringList getProjectFiles();

	KileProjectItem* activeProjectItem();
	/**
	 * Finds the project item for the file with URL @param url.
	 * @returns a pointer to the project item, 0 if this file does not belong to a project
	 **/
	KileProjectItem* itemFor(const KUrl &url, KileProject *project = NULL) const;
	KileProjectItem* itemFor(Info *docinfo, KileProject *project = NULL) const;
	KileProjectItem* selectProjectFileItem(const QString &label);
	QList<KileProjectItem*> selectProjectFileItems(const QString &label);

	QList<KileProjectItem*> itemsFor(Info *docinfo) const;

	static const KUrl symlinkFreeURL(const KUrl& url);

protected:
	void mapItem(TextInfo *docinfo, KileProjectItem *item);

	void trashDoc(TextInfo *docinfo, KTextEditor::Document *doc = NULL);

	TextInfo* createTextDocumentInfo(KileDocument::Type type, const KUrl &url, const KUrl& baseDirectory = KUrl());
	void recreateTextDocumentInfo(TextInfo *oldinfo);

	/**
	 * Tries to remove and delete a TextInfo object. The TextInfo object will only be deleted if it isn't referenced
	 * by any project item or if is is only referenced by a project that should be closed.
	 * @param closingproject Indicates whether the TextInfo object should be removed as part of a project close
	 *                       operation.
	 * @warning This method does not close or delete any Kate documents that are associated with the TextInfo object !
	 **/
	bool removeTextDocumentInfo(TextInfo *docinfo, bool closingproject = false);
	KTextEditor::Document* createDocument(const QString& name, const KUrl& url, TextInfo *docinfo, const QString& encoding, const QString& highlight);

	/**
	 *  Creates a document with the specified text.
	 * 
	 *  @param extension The extension of the file that should be created without leading "."
	 **/
	KTextEditor::View* createDocumentWithText(const QString& text, KileDocument::Type type = KileDocument::Text, const QString& extension = QString(), const KUrl& baseDirectory = KUrl());

	KTextEditor::View* loadText(KileDocument::Type type, const QString& name, const KUrl &url, const QString& encoding = QString(), bool create = true, const QString & highlight  = QString(), const QString &text = QString(), int index = -1, const KUrl& baseDirectory = KUrl());
	KTextEditor::View* loadItem(KileDocument::Type type, KileProjectItem *item, const QString& text = QString(), bool openProjectItemViews = true);

private:
	KTextEditor::Editor		*m_editor;
	QList<TextInfo*>		m_textInfoList;
	KileInfo			*m_ki;
	QList<KileProject*>		m_projects;
	KProgressDialog			*m_progressDialog;
	
	void dontOpenWarning(KileProjectItem *item, const QString &action, const QString &filetype);
	void cleanupDocumentInfoForProjectItems(KileDocument::Info *info);
	void createProgressDialog();

	QStringList autosaveWarnings;

};

}

#endif