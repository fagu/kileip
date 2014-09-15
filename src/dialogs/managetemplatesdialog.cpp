/*****************************************************************************************
    begin                : Sun Apr 27 2003
    copyright            : (C) 2003 by Jeroen Wijnhout (wijnhout@science.uva.nl)
                               2007 by Michel Ludwig (michel.ludwig@kdemail.net)
 *****************************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "dialogs/managetemplatesdialog.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QLayout>
#include <QLabel>
#include <QTreeWidget>

#include <KIconDialog>
#include <KIconLoader>
#include <KLineEdit>
#include <KLocalizedString>
#include <KMessageBox>
#include <QPushButton>

#include <QUrl>

#include <KIO/NetAccess>
#include <KConfigGroup>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStandardPaths>

#include "kiledebug.h"
#include "kileextensions.h"
#include "kileinfo.h"
#include "templates.h"

class TemplateListViewItem : public QTreeWidgetItem {
	public:
		TemplateListViewItem(QTreeWidget* parent, QTreeWidgetItem* preceding, const QString& mode, const KileTemplate::Info& info) : QTreeWidgetItem(parent, preceding), m_info(info) {
			setText(0, mode);
			setText(1, info.name);
			setText(2, KileInfo::documentTypeToString(info.type));
		}

		virtual ~TemplateListViewItem() {
		}

		KileTemplate::Info getTemplateInfo() {
			return m_info;
		}

	protected:
		KileTemplate::Info m_info;
};

// dialog to create a template
ManageTemplatesDialog::ManageTemplatesDialog(KileTemplate::Manager *templateManager, const QUrl &sourceURL, const QString &caption, QWidget *parent, const char *name)
		: QDialog(parent),
		m_templateManager(templateManager), m_sourceURL(sourceURL)
{
	setObjectName(name);
	setWindowTitle(caption);
	setModal(true);
	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	QWidget *mainWidget = new QWidget(this);
	QVBoxLayout *mainLayout = new QVBoxLayout;
	setLayout(mainLayout);
	mainLayout->addWidget(mainWidget);
	QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
	okButton->setDefault(true);
	okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
	connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
	//PORTING SCRIPT: WARNING mainLayout->addWidget(buttonBox) must be last item in layout. Please move it.
	mainLayout->addWidget(buttonBox);
	okButton->setDefault(true);

	m_templateType = KileDocument::Extensions().determineDocumentType(sourceURL);

	QWidget *page = new QWidget(this);
	page->setObjectName("managetemplates_mainwidget");
	mainLayout->addWidget(page);
	QGridLayout *topLayout = new QGridLayout();
	topLayout->setMargin(0);
//TODO PORT QT5 	topLayout->setSpacing(QDialog::spacingHint());
	page->setLayout(topLayout);

	topLayout->addWidget(new QLabel(i18n("Name:"), page), 0, 0);

	QString fileName = m_sourceURL.fileName();
	//remove the extension
	int dotPos = fileName.lastIndexOf('.');
	if (dotPos >= 0) {
		fileName = fileName.mid(0, dotPos);
	}
	m_nameEdit = new KLineEdit(fileName, page);
	mainLayout->addWidget(m_nameEdit);
	topLayout->addWidget(m_nameEdit, 0, 1);

	topLayout->addWidget(new QLabel(i18n("Type: %1", KileInfo::documentTypeToString(m_templateType)), page), 0, 2);
	topLayout->addWidget(new QLabel(i18n("Icon:"), page), 1, 0);

	m_iconEdit = new KLineEdit(QStandardPaths::locate(QStandardPaths::DataLocation, "pics/type_Default.png"), page);
	mainLayout->addWidget(m_iconEdit);
	topLayout->addWidget(m_iconEdit, 1, 1);

	QPushButton *iconbut = new QPushButton(i18n("Select..."), page);
	mainLayout->addWidget(iconbut);
	topLayout->addWidget(iconbut, 1, 2);

	m_templateList = new QTreeWidget(page);
	mainLayout->addWidget(m_templateList);
	m_templateList->setSortingEnabled(false);
	m_templateList->setHeaderLabels(QStringList() << i18nc("marked", "M")
																	<< i18n("Existing Templates")
																	<< i18n("Document Type"));
	m_templateList->setAllColumnsShowFocus(true);
	m_templateList->setRootIsDecorated(false);

	populateTemplateListView(m_templateType);

	topLayout->addWidget(m_templateList, 2, 0, 1, 3);

	m_showAllTypesCheckBox = new QCheckBox(i18n("Show all the templates"), page);
	mainLayout->addWidget(m_showAllTypesCheckBox);
	m_showAllTypesCheckBox->setChecked(false);
	connect(m_showAllTypesCheckBox, SIGNAL(toggled(bool)), this, SLOT(updateTemplateListView(bool)));
	topLayout->addWidget(m_showAllTypesCheckBox, 3, 0, 1, 2);

	QPushButton *clearSelectionButton = new QPushButton(page);
	mainLayout->addWidget(clearSelectionButton);
	clearSelectionButton->setIcon(QIcon::fromTheme("edit-clear-locationbar"));
	int buttonSize = clearSelectionButton->sizeHint().height();
	clearSelectionButton->setFixedSize(buttonSize, buttonSize);
	clearSelectionButton->setToolTip(i18n("Clear Selection"));
	connect(clearSelectionButton, SIGNAL(clicked()), this, SLOT(clearSelection()));
	topLayout->addWidget(clearSelectionButton, 3, 2, Qt::AlignRight);

	topLayout->addWidget(new QLabel(i18n("Select an existing template if you want to overwrite it with your new template.\nNote that you cannot overwrite templates marked with an asterisk:\nif you do select such a template, a new template with the same name\nwill be created in a location you have write access to."), page), 4, 0, 1, 3);

	connect(m_templateList, SIGNAL(itemClicked(QTreeWidgetItem*, int)), this, SLOT(slotSelectedTemplate(QTreeWidgetItem*)));
	connect(iconbut, SIGNAL(clicked()), this, SLOT(slotSelectIcon()));
	connect(this, SIGNAL(aboutToClose()), this, SLOT(addTemplate()));
}

// dialog to remove a template
ManageTemplatesDialog::ManageTemplatesDialog(KileTemplate::Manager *templateManager, const QString &caption, QWidget *parent, const char *name)
		: QDialog(parent),
		m_templateManager(templateManager), m_templateType(KileDocument::Undefined), m_showAllTypesCheckBox(NULL)
{
	setObjectName(name);
	setWindowTitle(caption);
	setModal(true);
	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
	okButton->setDefault(true);
	okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
	connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
	//PORTING SCRIPT: WARNING mainLayout->addWidget(buttonBox) must be last item in layout. Please move it.
// 	mainLayout->addWidget(buttonBox);
	okButton->setDefault(true);

	QWidget *page = new QWidget(this);
	page->setObjectName("managetemplates_mainwidget");
//TODO KF5
// 	mainLayout->addWidget(page);
	QVBoxLayout *topLayout = new QVBoxLayout();
	topLayout->setMargin(0);
//TODO PORT QT5 	topLayout->setSpacing(QDialog::spacingHint());
	page->setLayout(topLayout);

	m_templateList = new QTreeWidget(page);
//TODO KF5
// 	mainLayout->addWidget(m_templateList);
	m_templateList->setSortingEnabled(false);
	m_templateList->setHeaderLabels(QStringList() << i18nc("marked", "M")
	                                              << i18n("Existing Templates")
	                                              << i18n("Document Type"));
	m_templateList->setAllColumnsShowFocus(true);
	m_templateList->setRootIsDecorated(false);

	populateTemplateListView(KileDocument::Undefined);

	topLayout->addWidget(m_templateList);
	topLayout->addWidget(new QLabel(i18n("Please select the template that you want to remove.\nNote that you cannot delete templates marked with an asterisk (for which you lack the necessary deletion permissions)."), page));

	connect(this, SIGNAL(aboutToClose()), this, SLOT(removeTemplate()));
}

ManageTemplatesDialog::~ManageTemplatesDialog(){
}

void ManageTemplatesDialog::updateTemplateListView(bool showAllTypes) {
	populateTemplateListView((showAllTypes ? KileDocument::Undefined : m_templateType));
}

void ManageTemplatesDialog::clearSelection() {
	m_templateList->clearSelection();
}

//Adapt code and connect okbutton or other to new slot. It doesn't exist in qdialog
//Adapt code and connect okbutton or other to new slot. It doesn't exist in qdialog
void ManageTemplatesDialog::slotButtonClicked(int button)
{
// 	if (button == Ok) {
// 		emit aboutToClose();
// 	}
// //Adapt code and connect okbutton or other to new slot. It doesn't exist in qdialog
// //Adapt code and connect okbutton or other to new slot. It doesn't exist in qdialog
// 	QDialog::slotButtonClicked(button);
}

void ManageTemplatesDialog::populateTemplateListView(KileDocument::Type type) {
	m_templateManager->scanForTemplates();
	KileTemplate::TemplateList templateList = m_templateManager->getTemplates(type);
	QString mode;
	QTreeWidgetItem* previousItem = NULL;

	m_templateList->clear();
	for (KileTemplate::TemplateListIterator i = templateList.begin(); i != templateList.end(); ++i)
	{
		KileTemplate::Info info = *i;
		QFileInfo iconFileInfo(info.icon);
		mode = (QFileInfo(info.path).isWritable() && (!iconFileInfo.exists() || iconFileInfo.isWritable())) ? " " : "*";
		if ((type == KileDocument::Undefined) || (info.type == type)) {
			previousItem = new TemplateListViewItem(m_templateList, previousItem, mode, info);
		}
	}

	m_templateList->resizeColumnToContents(0);
	m_templateList->resizeColumnToContents(1);
}

void ManageTemplatesDialog::slotSelectedTemplate(QTreeWidgetItem *item) {
	TemplateListViewItem *templateItem = dynamic_cast<TemplateListViewItem*>(item);
	if (templateItem) {
		KileTemplate::Info info = templateItem->getTemplateInfo();
		m_nameEdit->setText(info.name);
		m_iconEdit->setText(info.icon);
	}
}

void ManageTemplatesDialog::slotSelectIcon() {
	KIconDialog *dlg = new KIconDialog();
	QString res = dlg->openDialog();
	KIconLoader kil;

	if (!res.isNull()) {
		m_iconEdit->setText(kil.iconPath(res, -KIconLoader::SizeLarge, false));
	}
}

void ManageTemplatesDialog::addTemplate() {

	QString templateName = (m_nameEdit->text()).trimmed();

	if (templateName.isEmpty()) {
		KMessageBox::error(this, i18n("The template name that you have entered is invalid.\nPlease enter a new name."));
		return;
	}

	QString icon = (m_iconEdit->text()).trimmed();
	QUrl iconURL = QUrl::fromUserInput(icon);

	if (icon.isEmpty()) {
		KMessageBox::error(this, i18n("Please choose an icon first."));
		return;
	}

	if (!KIO::NetAccess::exists(iconURL, KIO::NetAccess::SourceSide, this)) {
		KMessageBox::error(this, i18n("The icon file: %1\ndoes not seem to exist. Please choose a new icon.", icon));
		return;
	}

	if (!KIO::NetAccess::exists(m_sourceURL, KIO::NetAccess::SourceSide, this)) {
		KMessageBox::error(this, i18n("The file: %1\ndoes not seem to exist. Maybe you forgot to save the file?", m_sourceURL.toString()));
		return;
	}

	QTreeWidgetItem* item = m_templateList->currentItem();

	if (!item && m_templateManager->searchForTemplate(templateName, m_templateType)) {
		KMessageBox::error(this, i18n("A template named \"%1\" already exists.\nPlease remove it first.", templateName));
		return;
	}

	bool returnValue;
	if (item) {
		TemplateListViewItem *templateItem = dynamic_cast<TemplateListViewItem*>(item);
		Q_ASSERT(templateItem);
		KileTemplate::Info templateInfo = templateItem->getTemplateInfo();
		if (KMessageBox::warningYesNo(this, i18n("You are about to replace the template \"%1\"; are you sure?", templateInfo.name)) == KMessageBox::No) {
			reject();
			return;
		}
		returnValue = m_templateManager->replace(templateInfo, m_sourceURL, templateName, iconURL);
	}
	else {
		returnValue = m_templateManager->add(m_sourceURL, templateName, iconURL);
	}
	if (!returnValue) {
		KMessageBox::error(this, i18n("Failed to create the template."));
		reject();
		return;
	}
	accept();
}

bool ManageTemplatesDialog::removeTemplate()
{
	QTreeWidgetItem* item = m_templateList->currentItem();
	if (!item) {
		KMessageBox::information(this, i18n("Please select a template that should be removed."));
		return true;
	}

	TemplateListViewItem *templateItem = dynamic_cast<TemplateListViewItem*>(item);
	Q_ASSERT(templateItem);

	KileTemplate::Info templateInfo = templateItem->getTemplateInfo();

	if (!(KIO::NetAccess::exists(QUrl::fromUserInput(templateInfo.path), KIO::NetAccess::DestinationSide, this) && (KIO::NetAccess::exists(QUrl::fromUserInput(templateInfo.icon), KIO::NetAccess::DestinationSide, this) || !QFileInfo(templateInfo.icon).exists()))) {
		KMessageBox::error(this, i18n("Sorry, but you do not have the necessary permissions to remove the selected template."));
		return false;
	}

	if (KMessageBox::warningYesNo(this, i18n("You are about to remove the template \"%1\"; are you sure?", templateInfo.name)) == KMessageBox::No) {
		return false;
	}

	if (!m_templateManager->remove(templateInfo))
	{
		KMessageBox::error(this, i18n("The template could not be removed."));
		reject();
		return false;
	}
	accept();
	return true;
}

#include "managetemplatesdialog.moc"
