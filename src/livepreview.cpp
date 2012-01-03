/********************************************************************************
  Copyright (C) 2011-2012 by Michel Ludwig (michel.ludwig@kdemail.net)
 ********************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "livepreview.h"

#include "config.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHBoxLayout>
#include <QMap>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>

#include <KActionCollection>
#include <KLocale>
#include <KStandardDirs>
#include <KTempDir>
#include <KTextEditor/CodeCompletionInterface>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KToolBar>

#ifdef HAVE_VIEWERINTERFACE_H
#include <okular/interfaces/viewerinterface.h>
#endif

#include "kiledebug.h"
#include "kiletool_enums.h"
#include "kiledocmanager.h"
#include "kileviewmanager.h"
#include "widgets/logwidget.h"

//TODO: it still has to be checked whether it is necessary to use LaTeXInfo objects

namespace KileTool
{

class LivePreviewManager::PreviewInformation {
public:
	PreviewInformation()
	{
		initTemporaryDirectory();
	}

	~PreviewInformation() {
		delete m_tempDir;
	}

	QString getTempDir() const {
		return m_tempDir->name();
	}

	void clearPreviewPathMappings() {
		pathToPreviewPathHash.clear();
		previewPathToPathHash.clear();
	}

	bool createSubDirectoriesForProject(KileProject *project, bool *containsInvalidRelativeItem = NULL) {
		if(containsInvalidRelativeItem) {
			*containsInvalidRelativeItem = false;
		}
		QList<KileProjectItem*> items = project->items();
		const QString tempCanonicalDir = QDir(m_tempDir->name()).canonicalPath();
		if(tempCanonicalDir.isEmpty()) {
			return false;
		}
		Q_FOREACH(KileProjectItem *item, items) {
			bool successful = true;
			const QString itemRelativeDir = QFileInfo(tempCanonicalDir + '/' + item->path()).path();
			const QString itemAbsolutePath = QDir(itemRelativeDir).absolutePath();
			if(itemAbsolutePath.isEmpty()) {
				successful = false;
			}
			else if(!itemAbsolutePath.startsWith(tempCanonicalDir)) {
				if(containsInvalidRelativeItem) {
					*containsInvalidRelativeItem = true;
				}
				successful = false; // we don't want to create directories below 'm_tempDir->name()'
			}
			else {
				successful = QDir().mkpath(itemAbsolutePath);
			}
			if(!successful) {
				return false;
			}
		}
		return true;
	}

private:
	KTempDir *m_tempDir;

	void initTemporaryDirectory() {
		// work around bug in the SyncTeX implementation of PDFTeX (can't rename file)
		// should be: KStandardDirs::locateLocal("tmp", "kile-livepreview")
		m_tempDir = new KTempDir(KStandardDirs::locateLocal("appdata", "livepreview/preview-"));
	}

public:
	QHash<QString, QString> pathToPreviewPathHash;
	QHash<QString, QString> previewPathToPathHash;
	QString previewFile;
	QHash<KileDocument::TextInfo*, QByteArray> textHash;
};

LivePreviewManager::LivePreviewManager(KileInfo *ki, KActionCollection *ac)
 : m_ki(ki),
   m_controlToolBar(NULL),
   m_previewStatusLed(NULL),
   m_previewForCurrentDocumentAction(NULL),
   m_runningLaTeXInfo(NULL), m_runningTextView(NULL), m_runningProject(NULL),
   m_runningPreviewInformation(NULL), m_shownPreviewInformation(NULL), m_masterDocumentPreviewInformation(NULL)
{
	connect(m_ki->viewManager(), SIGNAL(textViewActivated(KTextEditor::View*)),
	        this, SLOT(handleTextViewActivated(KTextEditor::View*)));
	connect(m_ki->viewManager(), SIGNAL(textViewClosed(KTextEditor::View*,bool)),
	        this, SLOT(handleTextViewClosed(KTextEditor::View*,bool)));
	connect(m_ki->toolManager(), SIGNAL(childToolSpawned(KileTool::Base*,KileTool::Base*)),
	        this, SLOT(handleSpawnedChildTool(KileTool::Base*, KileTool::Base*)));
	connect(m_ki->docManager(), SIGNAL(documentSavedAs(KTextEditor::View*, KileDocument::TextInfo*)),
	        this, SLOT(handleDocumentSavedAs(KTextEditor::View*, KileDocument::TextInfo*)));
	createActions(ac);
	createControlToolBar();

	m_ledBlinkingTimer = new QTimer(this);
	m_ledBlinkingTimer->setSingleShot(false);
	m_ledBlinkingTimer->setInterval(500);
	connect(m_ledBlinkingTimer, SIGNAL(timeout()), m_previewStatusLed, SLOT(toggle()));

	m_documentChangedTimer = new QTimer(this);
	m_documentChangedTimer->setSingleShot(true);
	connect(m_documentChangedTimer, SIGNAL(timeout()), this, SLOT(handleDocumentModificationTimerTimeout()));

	m_cursorPositionChangedTimer = new QTimer(this);
	m_cursorPositionChangedTimer->setSingleShot(true);
	connect(m_cursorPositionChangedTimer, SIGNAL(timeout()), this, SLOT(handleCursorPositionChangedTimeout()));

	showPreviewDisabled();
}

LivePreviewManager::~LivePreviewManager()
{
	KILE_DEBUG();

	deleteAllLivePreviewInformation();
}

void LivePreviewManager::createActions(KActionCollection *ac)
{
	m_synchronizeViewWithCursorAction = new KToggleAction(KIcon("document-swap"), i18n("Synchronize Cursor Position with Preview Document"), this);
	// just to get synchronization back when the sync feature is activated (again)
	connect(m_synchronizeViewWithCursorAction, SIGNAL(triggered(bool)), this, SLOT(synchronizeViewWithCursorActionTriggered(bool)));
	ac->addAction("synchronize_cursor_preview", m_synchronizeViewWithCursorAction);

	m_previewForCurrentDocumentAction = new KToggleAction(KIcon("document-preview"), i18n("Toggle Live Preview for Current Document or Project"), this);
	m_previewForCurrentDocumentAction->setChecked(true);
	connect(m_previewForCurrentDocumentAction, SIGNAL(triggered(bool)), this, SLOT(previewForCurrentDocumentActionTriggered(bool)));
	ac->addAction("preview_current_document", m_previewForCurrentDocumentAction);
}

void LivePreviewManager::synchronizeViewWithCursorActionTriggered(bool b)
{
	KTextEditor::View *view = m_ki->viewManager()->currentTextView();
	if(!b || !view) {
		Okular::ViewerInterface *v = dynamic_cast<Okular::ViewerInterface*>(m_ki->viewManager()->viewerPart());
		if(v) {
			v->clearLastShownSourceLocation();
		}
		return;
	}
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoFor(view->document()));
	if(latexInfo) {
		// showPreviewCompileIfNecessary synchronizes the view with the cursor
		showPreviewCompileIfNecessary(latexInfo, view);
	}
}

void LivePreviewManager::previewForCurrentDocumentActionTriggered(bool b)
{
	KTextEditor::View *view = m_ki->viewManager()->currentTextView();
	if(!view) {
		return;
	}
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoFor(view->document()));
	if(!latexInfo) {
		return;
	}
	LivePreviewUserStatusHandler *userStatusHandler;
	findPreviewInformation(latexInfo, NULL, &userStatusHandler);
	Q_ASSERT(userStatusHandler);

	userStatusHandler->setLivePreviewEnabled(b);

	if(b) {
		showPreviewCompileIfNecessary(latexInfo, view);
	}
	else {
		disablePreview();
	}
}

void LivePreviewManager::disablePreview()
{
	stopAndClearPreview();
	m_previewForCurrentDocumentAction->setChecked(false);
	m_ki->viewManager()->setLivePreviewModeForDocumentViewer(false);
}

void LivePreviewManager::stopAndClearPreview()
{
	KILE_DEBUG();
	stopLivePreview();
	clearLivePreview();
}

void LivePreviewManager::clearLivePreview()
{
	KILE_DEBUG();
	showPreviewDisabled();

	KParts::ReadOnlyPart *viewerPart = m_ki->viewManager()->viewerPart();
	if(m_shownPreviewInformation && viewerPart->url() == KUrl::fromPath(m_shownPreviewInformation->previewFile)) {
		viewerPart->closeUrl();
	}
	m_shownPreviewInformation = NULL;
}

void LivePreviewManager::stopLivePreview()
{
	m_documentChangedTimer->stop();
	m_ki->toolManager()->stopLivePreview();

	m_runningPathToPreviewPathHash.clear();
	m_runningPreviewPathToPathHash.clear();
	m_runningPreviewFile.clear();
	m_runningLaTeXInfo = NULL;
	m_runningProject = NULL;
	m_runningTextView = NULL;
	m_runningPreviewInformation = NULL;
	m_runningTextHash.clear();
}

void LivePreviewManager::deleteAllLivePreviewInformation()
{
	// first, we have to make sure that nothing is shown anymore,
	// and that no preview is running
	stopAndClearPreview();

	disablePreview();

	// and now we can delete all the 'PreviewInformation' objects
	delete m_masterDocumentPreviewInformation;
	m_masterDocumentPreviewInformation = NULL;

	for(QHash<KileDocument::LaTeXInfo*, PreviewInformation*>::iterator i = m_latexInfoToPreviewInformationHash.begin();
	    i != m_latexInfoToPreviewInformationHash.end(); ++i) {
		delete i.value();
	}

	for(QHash<KileProject*,PreviewInformation*>::iterator i = m_projectToPreviewInformationHash.begin();
	    i != m_projectToPreviewInformationHash.end(); ++i) {
		delete i.value();
	}
	m_latexInfoToPreviewInformationHash.clear();
	m_projectToPreviewInformationHash.clear();
}

void LivePreviewManager::readConfig(KConfig *config)
{
	Q_UNUSED(config);

	m_synchronizeViewWithCursorAction->setChecked(KileConfig::synchronizeCursorWithView());

	m_controlToolBar->setVisible(KileConfig::livePreviewEnabled());
	if(!KileConfig::livePreviewEnabled()) {
		deleteAllLivePreviewInformation();
	}
	else {
		refreshLivePreview(); // e.g. in case the live preview was disabled and no preview is
		                      // currently shown
	}
}

void LivePreviewManager::writeConfig()
{
	KileConfig::setSynchronizeCursorWithView(m_synchronizeViewWithCursorAction->isChecked());
}

QWidget* LivePreviewManager::getControlToolBar()
{
	return m_controlToolBar;
}

void LivePreviewManager::createControlToolBar()
{
	m_controlToolBar = new KToolBar(NULL, false, false);
	m_controlToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	m_controlToolBar->setFloatable(false);
	m_controlToolBar->setMovable(false);
	m_controlToolBar->setIconDimensions(KIconLoader::SizeSmall);

	m_controlToolBar->addAction(m_previewForCurrentDocumentAction);
	m_controlToolBar->addAction(m_synchronizeViewWithCursorAction);

	m_controlToolBar->addSeparator();

	m_previewStatusLed = new KLed(m_controlToolBar);
	m_controlToolBar->addWidget(m_previewStatusLed);
}

void LivePreviewManager::handleMasterDocumentChanged()
{
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	deleteAllLivePreviewInformation();
	refreshLivePreview();
}

void LivePreviewManager::handleCursorPositionChanged(KTextEditor::View *view, const KTextEditor::Cursor &pos)
{
	Q_UNUSED(view);
	Q_UNUSED(pos);
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	if(!m_synchronizeViewWithCursorAction->isChecked()) {
		return;
	}
	m_cursorPositionChangedTimer->start(100);
}

void LivePreviewManager::handleTextChanged(KTextEditor::Document *doc)
{
	Q_UNUSED(doc);
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	KILE_DEBUG();
	stopLivePreview();
	showPreviewOutOfDate();

	m_documentChangedTimer->start(KileConfig::livePreviewCompilationDelay());
}

void LivePreviewManager::handleDocumentModificationTimerTimeout()
{
	KILE_DEBUG();
	KTextEditor::View *view = m_ki->viewManager()->currentTextView();
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoFor(view->document()));
	if(!latexInfo) {
		return;
	}

	KTextEditor::CodeCompletionInterface *codeCompletionInterface = qobject_cast<KTextEditor::CodeCompletionInterface*>(view);

	// if the code completion box is currently shown, we don't trigger an update of the preview
	// as this will cause the document to be saved and the completion box to be hidden as a consequence
	if(codeCompletionInterface && codeCompletionInterface->isCompletionActive()) {
		m_documentChangedTimer->start();
		return;
	}

	LivePreviewUserStatusHandler *userStatusHandler;
	findPreviewInformation(latexInfo, NULL, &userStatusHandler);
	Q_ASSERT(userStatusHandler);
	if(userStatusHandler->isLivePreviewEnabled()) {
		compilePreview(latexInfo, view);
	}
}

void LivePreviewManager::showPreviewDisabled()
{
	KILE_DEBUG();
	m_ledBlinkingTimer->stop();
	if(m_previewStatusLed) {
		m_previewStatusLed->off();
	}
}

void LivePreviewManager::showPreviewRunning()
{
	KILE_DEBUG();
	if(m_previewStatusLed) {
		m_previewStatusLed->setColor(QColor(Qt::yellow));
		m_previewStatusLed->off();
	}
	m_ledBlinkingTimer->start();
}

void LivePreviewManager::showPreviewFailed()
{
	KILE_DEBUG();
	m_ledBlinkingTimer->stop();
	if(m_previewStatusLed) {
		m_previewStatusLed->on();
		m_previewStatusLed->setColor(QColor(Qt::red));
	}
}

void LivePreviewManager::showPreviewSuccessful()
{
	KILE_DEBUG();
	m_ledBlinkingTimer->stop();
	if(m_previewStatusLed) {
		m_previewStatusLed->on();
		m_previewStatusLed->setColor(QColor(Qt::green));
	}
}

void LivePreviewManager::showPreviewOutOfDate()
{
	KILE_DEBUG();
	m_ledBlinkingTimer->stop();
	if(m_previewStatusLed) {
		m_previewStatusLed->on();
		m_previewStatusLed->setColor(QColor(Qt::yellow));
	}

}

// If a LaTeXInfo* pointer is passed as first argument, it is guaranteed that '*userStatusHandler' won't be NULL.
LivePreviewManager::PreviewInformation* LivePreviewManager::findPreviewInformation(KileDocument::TextInfo *textInfo,
                                                                                   KileProject* *locatedProject,
                                                                                   LivePreviewUserStatusHandler* *userStatusHandler)
{
	const QString masterDocumentFileName = m_ki->getMasterDocumentFileName();
	if(locatedProject) {
		*locatedProject = NULL;
	}
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(textInfo);
	if(userStatusHandler) {
		if(latexInfo) {
			*userStatusHandler = latexInfo;
		}
		else {
			*userStatusHandler = NULL;
		}
	}
	if(!masterDocumentFileName.isEmpty()) {
		KILE_DEBUG() << "master document defined";
		return m_masterDocumentPreviewInformation;
	}
	KileProject *project = m_ki->docManager()->projectForMember(textInfo->url());
	if(project) {
		KILE_DEBUG() << "part of a project";
		if(locatedProject) {
			*locatedProject = project;
		}
		if(userStatusHandler) {
			*userStatusHandler = project;
		}
		if(m_projectToPreviewInformationHash.contains(project)) {
			KILE_DEBUG() << "project found";
			return m_projectToPreviewInformationHash[project];
		}
		else {
			KILE_DEBUG() << "project not found";
			return NULL;
		}
	}
	else if(latexInfo && m_latexInfoToPreviewInformationHash.contains(latexInfo)) {
		KILE_DEBUG() << "not part of a project";
		return m_latexInfoToPreviewInformationHash[latexInfo];
	}
	else {
		KILE_DEBUG() << "not found";
		return NULL;
	}
}

void LivePreviewManager::handleCursorPositionChangedTimeout()
{
	KTextEditor::View *view = m_ki->viewManager()->currentTextView();
	if(!view) {
		return;
	}
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoFor(view->document()));
	if(!latexInfo) {
		return;
	}
	LivePreviewUserStatusHandler *userStatusHandler = NULL;
	findPreviewInformation(latexInfo, NULL, &userStatusHandler);
	if(!userStatusHandler->isLivePreviewEnabled()) {
		return;
	}

	synchronizeViewWithCursor(latexInfo, view, view->cursorPosition());
}

void LivePreviewManager::synchronizeViewWithCursor(KileDocument::TextInfo *textInfo, KTextEditor::View *view, const KTextEditor::Cursor& newPosition)
{
	Q_UNUSED(view);
	KILE_DEBUG() << "new position " << newPosition;

	PreviewInformation *previewInformation = findPreviewInformation(textInfo);
	if(!previewInformation) {
		KILE_DEBUG() << "couldn't find preview information for" << textInfo;
		return;
	}

	QFileInfo updatedFileInfo(textInfo->getDoc()->url().toLocalFile());
	QString filePath;
	if(previewInformation->pathToPreviewPathHash.contains(updatedFileInfo.absoluteFilePath())) {
		KILE_DEBUG() << "found";
		filePath = previewInformation->pathToPreviewPathHash[updatedFileInfo.absoluteFilePath()];
	}
	else {
		KILE_DEBUG() << "not found";
		filePath = textInfo->getDoc()->url().toLocalFile();
	}
	KILE_DEBUG() << "filePath" << filePath;

	KILE_DEBUG() << "previewFile" << previewInformation->previewFile;

	if(!m_ki->viewManager()->viewerPart() || !QFile::exists(previewInformation->previewFile)) {
		return;
	}

	KILE_DEBUG() << "url" << m_ki->viewManager()->viewerPart()->url();

	KUrl previewUrl(KUrl::fromPath(previewInformation->previewFile));

	bool fileOpened = true;
	if(m_ki->viewManager()->viewerPart()->url().isEmpty() || m_ki->viewManager()->viewerPart()->url() != previewUrl) {
		KILE_DEBUG() << "loading again";
		if(m_ki->viewManager()->viewerPart()->openUrl(previewUrl)) {
			// don't forget this
			m_shownPreviewInformation = previewInformation;
		}
		else {
			fileOpened = false;
			clearLivePreview();
			// must happen after the call to 'clearLivePreview' only
			showPreviewFailed();
		}
	}

	if(fileOpened) {
		m_ki->viewManager()->showSourceLocationInDocumentViewer(filePath, newPosition.line(), newPosition.column());
	}
}

static QByteArray computeHashOfDocument(KTextEditor::Document *doc)
{
	QCryptographicHash cryptographicHash(QCryptographicHash::Sha1);
	cryptographicHash.addData(doc->text().toUtf8());
	// allows to catch situations when the URL of the document has changed,
	// e.g. after a save-as operation, which breaks the handling of source
	// references for the displayed document
	cryptographicHash.addData(doc->url().toEncoded());

	return cryptographicHash.result();
}

static void fillTextHashForProject(KileProject *project, QHash<KileDocument::TextInfo*, QByteArray> &textHash)
{
	QList<KileProjectItem*> list = project->items();
	for(QList<KileProjectItem*>::iterator it = list.begin(); it != list.end(); ++it) {
		KileProjectItem *item = *it;

		KileDocument::TextInfo *textInfo = item->getInfo();
		if(!textInfo) {
			continue;
		}
		KTextEditor::Document *document = textInfo->getDoc();
		if(!document) {
			continue;
		}
		textHash[textInfo] = computeHashOfDocument(document);
	}
}

void LivePreviewManager::fillTextHashForMasterDocument(QHash<KileDocument::TextInfo*, QByteArray> &textHash)
{
	// we compute hashes over all the opened files
	QList<KileDocument::TextInfo*> textDocumentInfos = m_ki->docManager()->textDocumentInfos();
	for(QList<KileDocument::TextInfo*>::iterator it = textDocumentInfos.begin(); it != textDocumentInfos.end(); ++it) {
		KileDocument::TextInfo *textInfo = *it;
		if(!textInfo) {
			continue;
		}
		KTextEditor::Document *document = textInfo->getDoc();
		if(!document) {
			continue;
		}
		textHash[textInfo] = computeHashOfDocument(document);
	}
}

void LivePreviewManager::showPreviewCompileIfNecessary(KileDocument::LaTeXInfo *latexInfo, KTextEditor::View *view)
{
	KILE_DEBUG();
	// first, stop any running live preview
	stopLivePreview();

	KileProject *project = NULL;
	PreviewInformation *previewInformation = findPreviewInformation(latexInfo, &project);
	if(!previewInformation) {
		KILE_DEBUG() << "not found";
		compilePreview(latexInfo, view);
	}
	else {
		QHash<KileDocument::TextInfo*, QByteArray> newHash;
// 		QString fileName;
// 		QFileInfo fileInfo(view->document()->url().path());
// 		if(previewInformation->pathToPreviewPathHash.contains(fileInfo.absoluteFilePath())) {
// 			KILE_DEBUG() << "contains";
// 			fileName = previewInformation->pathToPreviewPathHash[fileInfo.absoluteFilePath()];
// 		}
// 		else {
// 			KILE_DEBUG() << "does not contain";
// 			fileName = fileInfo.absoluteFilePath();
// 		}
// 		KILE_DEBUG() << "fileName:" << fileName;
		bool masterDocumentSet = !m_ki->getMasterDocumentFileName().isEmpty();

		if(masterDocumentSet) {
			fillTextHashForMasterDocument(newHash);
		}
		else if(project) {
			fillTextHashForProject(project, newHash);
		}
		else {
			newHash[latexInfo] = computeHashOfDocument(view->document());
		}

		if(newHash != previewInformation->textHash || !QFile::exists(previewInformation->previewFile)) {
			KILE_DEBUG() << "hashes don't match";
			compilePreview(latexInfo, view);
		}
		else {
			KILE_DEBUG() << "hashes match";
			showPreviewSuccessful();
			synchronizeViewWithCursor(latexInfo, view, view->cursorPosition());
		}
	}
}

void LivePreviewManager::compilePreview(KileDocument::LaTeXInfo *latexInfo, KTextEditor::View *view)
{
	KILE_DEBUG() << "updating preview";
	m_ki->viewManager()->setLivePreviewModeForDocumentViewer(true);
	m_runningPathToPreviewPathHash.clear();
	m_runningPreviewPathToPathHash.clear();

	//CAUTION: as saving launches an event loop, we don't want 'compilePreview'
	//         to be called from within 'compilePreview'
	m_documentChangedTimer->blockSignals(true);
	bool saveResult = m_ki->docManager()->fileSaveAll();
	m_documentChangedTimer->blockSignals(false);
	// first, we have to save the documents
	if(!saveResult) {
		displayErrorMessage(i18n("Some documents could not be saved correctly"));
		return;
	}

	// document is new and hasn't been saved yet at all
	if(view->document()->url().isEmpty()) {
		displayErrorMessage(i18n("The document must have been saved before the live preview can be started"));
		return;
	}

	// first, stop any running live preview
	stopLivePreview();

	KileProject *project = NULL;
	LivePreviewUserStatusHandler *userStatusHandler;
	PreviewInformation *previewInformation = findPreviewInformation(latexInfo, &project, &userStatusHandler);
	Q_ASSERT(userStatusHandler);
	if(!previewInformation) {
		previewInformation = new PreviewInformation();
		if(!m_ki->getMasterDocumentFileName().isEmpty()) {
			m_masterDocumentPreviewInformation = previewInformation;
		}
		else if(project) {
			bool containsInvalidRelativeItem = false;
			// in the case of a project, we might have to create a similar subdirectory
			// structure as it is present in the real project in order for LaTeX
			// to work correctly
			if(!previewInformation->createSubDirectoriesForProject(project, &containsInvalidRelativeItem)) {
				userStatusHandler->setLivePreviewEnabled(false);
				if(containsInvalidRelativeItem) {
					displayErrorMessage(i18n("The location of one project item is not relative to the project's base directory\n"
					                         "Live preview for this project has been disabled"), true);
				}
				else {
					displayErrorMessage(i18n("Failed to create the subdirectory structure"));
				}
				delete previewInformation;
				return;
			}
			m_projectToPreviewInformationHash[project] = previewInformation;
		}
		else {
			m_latexInfoToPreviewInformationHash[latexInfo] = previewInformation;
		}
	}

	connect(latexInfo, SIGNAL(aboutToBeDestroyed(KileDocument::TextInfo*)),
	        this, SLOT(removeLaTeXInfo(KileDocument::TextInfo*)),
	        Qt::UniqueConnection);

	if(project) {
		connect(project, SIGNAL(aboutToBeDestroyed(KileProject*)),
		        this, SLOT(removeProject(KileProject*)),
		        Qt::UniqueConnection);
		connect(project, SIGNAL(projectItemAdded(KileProject*,KileProjectItem*)),
		        this, SLOT(handleProjectItemAdded(KileProject*,KileProjectItem*)),
		        Qt::UniqueConnection);
		connect(project, SIGNAL(projectItemRemoved(KileProject*,KileProjectItem*)),
		        this, SLOT(handleProjectItemRemoved(KileProject*,KileProjectItem*)),
		        Qt::UniqueConnection);
	}

	KileTool::LivePreviewLaTeX *latex = dynamic_cast<KileTool::LivePreviewLaTeX *>(m_ki->toolManager()->createTool("LivePreviewPDFLaTeX", QString(), false));
	if(!latex) {
		KILE_DEBUG()<< "couldn't create the tool";
		return;
	}

	// important!
	latex->setPartOfLivePreview();
	connect(latex, SIGNAL(done(KileTool::Base*,int,bool)), this, SLOT(toolDone(KileTool::Base*,int,bool)));
	connect(latex, SIGNAL(destroyed()), this, SLOT(toolDestroyed()));

	QFileInfo fileInfo;
	const bool masterDocumentSet = !m_ki->getMasterDocumentFileName().isEmpty();
	if(masterDocumentSet) {
		fileInfo = QFileInfo(m_ki->getMasterDocumentFileName());
	}
	else if(project) {
		fileInfo = QFileInfo(m_ki->getCompileNameForProject(project));
	}
	else {
		fileInfo = QFileInfo(m_ki->getCompileName());
	}

	const QString inputDir = previewInformation->getTempDir() + ':' + fileInfo.absolutePath();

	// set value of texinput path (only for LivePreviewManager tools)
	QString texInputPath = KileConfig::teXPaths();
	if(!texInputPath.isEmpty()) {
		texInputPath = inputDir + ':' + texInputPath;
	}
	else {
		texInputPath = inputDir;
	}
	latex->setTeXInputPaths(texInputPath);

	QString bibInputPath = KileConfig::bibInputPaths();
	if(!bibInputPath.isEmpty()) {
		bibInputPath = inputDir + ':' + bibInputPath;
	}
	else {
		bibInputPath = inputDir;
	}
	latex->setBibInputPaths(bibInputPath);

	QString bstInputPath = KileConfig::bstInputPaths();
	if(!bstInputPath.isEmpty()) {
		bstInputPath = inputDir + ':' + bstInputPath;
	}
	else {
		bstInputPath = inputDir;
	}
	latex->setBstInputPaths(bstInputPath);

// 	m_runningPathToPreviewPathHash[fileInfo.absoluteFilePath()] = tempFile;
// 	m_runningPreviewPathToPathHash[tempFile] = fileInfo.absoluteFilePath();

	// don't emit the 'requestSaveAll' signal
// 	latex->removeFlag(EmitSaveAllSignal);

	latex->setTargetDir(previewInformation->getTempDir());
	latex->setSource(fileInfo.absoluteFilePath(), fileInfo.absolutePath());
// 	latex->setTargetDir(previewInformation->getTempDir());
	latex->prepareToRun();
// 	latex->launcher()->setWorkingDirectory(previewInformation->getTempDir());
	KILE_DEBUG() << "dir:" << previewInformation->getTempDir();

	m_runningTextView = view;
	m_runningLaTeXInfo = latexInfo;
	m_runningProject = project;
	m_runningPreviewFile = previewInformation->getTempDir() + '/' + latex->target();
	m_runningTextHash.clear();
	if(masterDocumentSet) {
		fillTextHashForMasterDocument(m_runningTextHash);
	}
	else if(project) {
		fillTextHashForProject(project, m_runningTextHash);
	}
	else {
		m_runningTextHash[latexInfo] = computeHashOfDocument(latexInfo->getDoc());
	}
	m_runningPreviewInformation = previewInformation;
	showPreviewRunning();

	// finally, run the tool
	m_ki->toolManager()->run(latex);
}

bool LivePreviewManager::isLivePreviewActive() const
{
	KParts::ReadOnlyPart *viewerPart = m_ki->viewManager()->viewerPart();

	return m_runningPreviewInformation
	       || (m_shownPreviewInformation
	           && viewerPart
	           && viewerPart->url() == KUrl::fromPath(m_shownPreviewInformation->previewFile));
}

bool LivePreviewManager::isLivePreviewPossible() const
{
#ifdef LIVEPREVIEW_POSSIBLE
	return true;
#else
	return false;
#endif
}

void LivePreviewManager::handleTextViewActivated(KTextEditor::View *view, bool clearPreview)
{
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}
	if(clearPreview) {
		stopAndClearPreview();
	}
	else {
		stopLivePreview();
	}
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoFor(view->document()));
	if(!latexInfo) {
		return;
	}
	m_documentChangedTimer->stop();

	LivePreviewUserStatusHandler *userStatusHandler = NULL;
	findPreviewInformation(latexInfo, NULL, &userStatusHandler);
	Q_ASSERT(userStatusHandler);
	const bool livePreviewActive = userStatusHandler->isLivePreviewEnabled();

	// update the state of the live preview control button
	m_previewForCurrentDocumentAction->setChecked(livePreviewActive);
	if(!livePreviewActive) {
		disablePreview();
	}
	else {
		showPreviewCompileIfNecessary(latexInfo, view);
	}
}

void LivePreviewManager::handleTextViewClosed(KTextEditor::View *view, bool wasActiveView)
{
	Q_UNUSED(view);
	Q_UNUSED(wasActiveView);
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	m_cursorPositionChangedTimer->stop();

	// check if there is still an open editor tab
	if(!m_ki->viewManager()->activeView()) {
		stopAndClearPreview();
	}
}

void LivePreviewManager::refreshLivePreview()
{
	KTextEditor::View *textView = m_ki->viewManager()->currentTextView();
	if(!textView) {
		KILE_DEBUG() << "no text view is shown; hence, no preview can be shown";
		return;
	}
	handleTextViewActivated(textView, false); // don't automatically clear the preview
}

void LivePreviewManager::removeLaTeXInfo(KileDocument::LaTeXInfo *latexInfo)
{
	if(!m_latexInfoToPreviewInformationHash.contains(latexInfo)) {
		return; // nothing to be done
	}

	PreviewInformation *previewInformation = m_latexInfoToPreviewInformationHash[latexInfo];

	if(m_runningLaTeXInfo == latexInfo) {
		stopLivePreview();
	}

	if(previewInformation == m_shownPreviewInformation) {
		clearLivePreview();
	}

	m_latexInfoToPreviewInformationHash.remove(latexInfo);
	delete previewInformation;
}

void LivePreviewManager::removeProject(KileProject *project)
{
	if(!m_projectToPreviewInformationHash.contains(project)) {
		return; // nothing to be done
	}

	PreviewInformation *previewInformation = m_projectToPreviewInformationHash[project];

	if(m_runningProject == project) {
		stopLivePreview();
	}

	if(previewInformation == m_shownPreviewInformation) {
		clearLivePreview();
	}

	m_projectToPreviewInformationHash.remove(project);
	delete previewInformation;
}


void LivePreviewManager::handleProjectItemAdditionOrRemoval(KileProject *project, KileProjectItem *item)
{
	KILE_DEBUG();
	bool previewNeedsToBeRefreshed = false;

	// we can't use TextInfo pointers here as they might not be set in 'item' yet
	KileDocument::LaTeXInfo *latexInfo = dynamic_cast<KileDocument::LaTeXInfo*>(m_ki->docManager()->textInfoForURL(item->url()));
	if(latexInfo && m_latexInfoToPreviewInformationHash.contains(latexInfo)) {
		PreviewInformation *previewInformation = m_latexInfoToPreviewInformationHash[latexInfo];
		if(previewInformation == m_shownPreviewInformation) {
			previewNeedsToBeRefreshed = true;
		}
		removeLaTeXInfo(latexInfo);
	}

	if(m_projectToPreviewInformationHash.contains(project)) {
		PreviewInformation *previewInformation = m_projectToPreviewInformationHash[project];
		if(previewInformation == m_shownPreviewInformation) {
			previewNeedsToBeRefreshed = true;
		}
		removeProject(project);
	}

	// finally, check whether the currently activated text view is the 'modified' project item
	if(!previewNeedsToBeRefreshed) {
		KTextEditor::View *view = m_ki->viewManager()->currentTextView();
		// we can't use TextInfo pointers here as they might not be set in 'item' yet
		if(view->document()->url() == item->url()) {
			previewNeedsToBeRefreshed = true;
		}
	}

	KILE_DEBUG() << "previewNeedsToBeRefreshed" << previewNeedsToBeRefreshed;
	if(previewNeedsToBeRefreshed) {
		// we can't do this here directly as 'item' might not be fully set up yet (e.g., if it has been added)
		QTimer::singleShot(0, this, SLOT(refreshLivePreview()));
	}
}

void LivePreviewManager::handleProjectItemAdded(KileProject *project, KileProjectItem *item)
{
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}
	KILE_DEBUG();

	// the directory structure in the temporary directory will be updated when
	// 'compilePreview' is called; 'handleProjectItemAdditionOrRemoval' will delete
	// PreviewInformation objects
	handleProjectItemAdditionOrRemoval(project, item);
}

void LivePreviewManager::handleProjectItemRemoved(KileProject *project, KileProjectItem *item)
{
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	KILE_DEBUG();
	handleProjectItemAdditionOrRemoval(project, item);
}

void LivePreviewManager::handleDocumentSavedAs(KTextEditor::View *view, KileDocument::TextInfo *info)
{
	if(!KileConfig::livePreviewEnabled()) {
		return;
	}

	Q_UNUSED(info);
	KTextEditor::View *currentTextView = m_ki->viewManager()->currentTextView();
	if(view != currentTextView) { // might maybe happen at some point...
		// preview will be refreshed the next time that view is activated as the hashes don't
		// match anymore
		return;
	}
	refreshLivePreview();
}

void LivePreviewManager::toolDestroyed()
{
	KILE_DEBUG() << "\tLivePreviewManager: tool destroyed" << endl;
}

void LivePreviewManager::handleSpawnedChildTool(KileTool::Base *parent, KileTool::Base *child)
{
	Q_UNUSED(parent);
	KILE_DEBUG();
	connect(child, SIGNAL(done(KileTool::Base*,int,bool)), this, SLOT(childToolDone(KileTool::Base*,int,bool)));
}

void LivePreviewManager::toolDone(KileTool::Base *base, int i, bool childToolSpawned)
{
	KILE_DEBUG() << "\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << i << endl;
	KILE_DEBUG() << "\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << i << endl;
	KILE_DEBUG() << "\tLivePreviewManager: tool done" << base->name() << i << childToolSpawned <<  endl;
	if(i != Success) {
		KILE_DEBUG() << "tool didn't return successfully, doing nothing";
		showPreviewFailed();
	}
	// a LaTeX variant must have finished for the preview to be complete
	else if(!childToolSpawned && dynamic_cast<KileTool::LaTeX*>(base)) {
		updatePreviewInformationAfterCompilationFinished();
	}
}

void LivePreviewManager::childToolDone(KileTool::Base *base, int i, bool childToolSpawned)
{
	KILE_DEBUG() << "\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << i << endl;
	KILE_DEBUG() << "\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << i << endl;
	KILE_DEBUG() << "\tLivePreviewManager: child tool done" << base->name() << i << childToolSpawned << endl;
	if(!m_ki->viewManager()->viewerPart()) {
		return;
	}
	if(i != Success) {
		KILE_DEBUG() << "tool didn't return successfully, doing nothing";
		showPreviewFailed();
	}
	// a LaTeX variant must have finished for the preview to be complete
	else if(!childToolSpawned && dynamic_cast<KileTool::LaTeX*>(base)) {
		updatePreviewInformationAfterCompilationFinished();
	}
}

void LivePreviewManager::updatePreviewInformationAfterCompilationFinished()
{
	m_shownPreviewInformation = m_runningPreviewInformation;
	m_shownPreviewInformation->pathToPreviewPathHash = m_runningPathToPreviewPathHash;
	m_shownPreviewInformation->previewPathToPathHash = m_runningPreviewPathToPathHash;
	m_shownPreviewInformation->textHash = m_runningTextHash;
	m_shownPreviewInformation->previewFile = m_runningPreviewFile;
	if(m_ki->viewManager()->viewerPart() && QFile::exists(m_shownPreviewInformation->previewFile)) {
		if(m_ki->viewManager()->viewerPart()->openUrl(KUrl::fromPath(m_shownPreviewInformation->previewFile))) {
			synchronizeViewWithCursor(m_runningLaTeXInfo, m_runningTextView, m_runningTextView->cursorPosition());
			showPreviewSuccessful();
		}
		else {
			clearLivePreview();
			// must happen after the call to 'clearLivePreview' only
			showPreviewFailed();
		}
	}
}

void LivePreviewManager::displayErrorMessage(const QString &text, bool clearFirst)
{
	if(clearFirst) {
		m_ki->logWidget()->clear();
	}
	m_ki->logWidget()->printMessage(KileTool::Error, text, i18n("LivePreview"));
}

}

#include "livepreview.moc"
