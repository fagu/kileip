/***************************************************************************
    date                 : Mar 05 2005
    version              : 0.11
    copyright            : (C) 2004-2005 by Holger Danielsson
    email                : holger.danielsson@t-online.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qfileinfo.h>

#include <kate/view.h>
#include <kate/document.h>
#include <ktexteditor/searchinterface.h>
#include <ktexteditor/editinterfaceext.h>
#include <klocale.h>
#include <kinputdialog.h>

#include "kileinfo.h"
#include "kileviewmanager.h"
#include "kileconfig.h"
#include "kileactions.h"

#include "kileedit.h"

namespace KileDocument
{

EditorExtension::EditorExtension(KileInfo *info) : m_ki(info)
{
	m_complete = new KileDocument::CodeCompletion(m_ki);

	// init regexp
	m_reg.setPattern("\\\\(begin|end)\\s*\\{\\s*([A-Za-z]+\\*?)\\s*\\}");

	// init environments
	QStringList listenv;
	listenv << "description"  << "enumerate"  << "itemize"
	        << "description*" << "enumerate*" << "itemize*"         // mdwtab
	        << "Bdescription" << "Benumerate" << "Bitemize"         // fancybox
	        << "labeling"                                           // koma-script
	        ; 
	/*
	QStringList mathenv;
	mathenv << "align"  << "alignat" << << "alignat*" 
	        << "aligned" << "alignedat"
	        << "bmatrix"
	        << "cases"
	        << "eqnarray" << "eqnarray*"
	        << "flalign" << "flalign*"
	        << "gather" << "gather*" << "gathered"
	        << "matrix" << "multline" << "multline*"
	        << "pmatrix"
	        << "split"
	        << "vmatrix" << "Vmatrix"
	        << "xalignat" << "xalignat*" << "xxalignat"<< "xxalignat*"
	        ;
	 
	QStringList tabularenv;
	tabularenv << "array" << "tabular" << "tabular*" << "tabbing"   
	           << "longtable"                                       // longtable
	           << "supertabular" << "supertabular*"                 // supertab
	           << "mpsupertabular*" << "mpsupertabular*"            // supertab
	           << "tabularx"                                        // tabularx             
	           << "xtabular" << "mpxtabular"                        // xtab
	           << "blockarray" << "block"                           // blkarray
	           ;
	*/
	
	// set standard environments
	setEnvironment(listenv,m_dictListEnv);
	// setEnvironment(mathenv,m_dictMathEnv);
	// setEnvironment(tabularenv,m_dictTabularEnv);

	readConfig();
}

EditorExtension::~EditorExtension()
{
	delete m_complete;
}

//////////////////// read configuration ////////////////////

void EditorExtension::readConfig(void)
{
	// config section
	setEnvironment(KileConfig::envList(), m_dictListEnv);
	setEnvironment(KileConfig::envMath(), m_dictMathEnv);
	setEnvironment(KileConfig::envTabular(), m_dictTabularEnv);
}

void EditorExtension::insertTag(const KileAction::TagData& data, Kate::View *view)
{
	Kate::Document *doc = view->getDoc();
	if ( !doc) return;

	//whether or not to wrap tag around selection
	bool wrap = ( (!data.tagEnd.isNull()) && doc->hasSelection());

	//%C before or after the selection
	bool before = data.tagBegin.contains("%C");
	bool after = data.tagEnd.contains("%C");

	//save current cursor position
	int para=view->cursorLine();
	int para_begin=para;
	int index=view->cursorColumnReal();
	int index_begin=index;
	int para_end=0;
	int index_cursor=index;
	int para_cursor=index;

	//if there is a selection act as if cursor is at the beginning of selection
	if (wrap)
	{
		index = doc->selStartCol();
		para  = doc->selStartLine();
		para_end = doc->selEndLine();
	}

	QString ins = data.tagBegin;

	//start an atomic editing sequence
	KTextEditor::EditInterfaceExt *editInterfaceExt = KTextEditor::editInterfaceExt( doc );
	if ( editInterfaceExt ) editInterfaceExt->editBegin();

	//cut the selected text
	if (wrap)
	{
		ins += doc->selection();
		doc->removeSelectedText();
	}

	ins += data.tagEnd;

	//do some replacements
	QFileInfo fi( doc->url().path());
	ins.replace("%S", fi.baseName(true));

	//insert first part of tag at cursor position
	doc->insertText(para,index,ins);

	//move cursor to the new position
	if ( before || after )
	{
		int n = data.tagBegin.contains("\n")+ data.tagEnd.contains("\n");
		if (wrap) n += para_end > para ? para_end-para : para-para_end;
		for (int line = para_begin; line <= para_begin+n; ++line)
		{
			if (doc->textLine(line).contains("%C"))
			{
				int i=doc->textLine(line).find("%C");
				para_cursor = line; index_cursor = i;
				doc->removeText(line,i,line,i+2);
				break;
			}
			index_cursor=index;
			para_cursor=line;
		}
	}
	else
	{
		int py = para_begin, px = index_begin;
		if (wrap) //act as if cursor was at beginning of selected text (which is the point where the tagBegin is inserted)
		{
			py = para;
			px = index;
		}
		para_cursor = py+data.dy; index_cursor = px+data.dx;
	}

	//end the atomic editing sequence
	if ( editInterfaceExt ) editInterfaceExt->editEnd();

	//set the cursor position (it is important that this is done outside of the atomic editing sequence)
	view->setCursorPositionReal(para_cursor, index_cursor);

	doc->clearSelection();
}
//////////////////// list/math/tabular environments ////////////////////

void EditorExtension::setEnvironment(const QStringList &list, QMap<QString,bool> &map)
{
	for (uint i=0; i<list.count(); ++i)
		map[list[i]] = true;
}

bool EditorExtension::isListEnvironment(const QString &name)
{
	return m_dictListEnv.contains(name);
}

bool EditorExtension::isMathEnvironment(const QString &name)
{
	return m_dictMathEnv.contains(name);
}

bool EditorExtension::isTabEnvironment(const QString &name)
{
	return m_dictTabularEnv.contains(name);
}

//////////////////// goto environment tag (begin or end) ////////////////////

// goto the next non-nested environment tag

Kate::View* EditorExtension::determineView(Kate::View *view)
{
	if (view == 0L)
		view = m_ki->viewManager()->currentView();

	m_overwritemode = (view == 0L) ? false : view->isOverwriteMode();

	return view;
}

void EditorExtension::gotoEnvironment(bool backwards, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	EnvData env;
	bool found;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	// start searching
	if ( backwards )
	{
		found = findBeginEnvironment(doc,row,col,env);
		//kdDebug() << "   goto begin env:  " << env.row << "/" << env.col << endl;
	
	}
	else
	{
		found = findEndEnvironment(doc,row,col,env);
		if ( !m_overwritemode )
		env.col += env.len;
	}
	
	if ( found )
		view->setCursorPositionReal(env.row,env.col);
}

// match the opposite environment tag

void EditorExtension::matchEnvironment(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	EnvData env;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	// we only start, when we are at an environment tag
	if ( !isEnvironmentPosition(doc,row,col,env) )
		return;
	
	gotoEnvironment( env.tag != EnvBegin,view);
}

//////////////////// close an open environment  ////////////////////

// search for the last opened environment and close it

void EditorExtension::closeEnvironment(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	uint row,col;
	QString name;
	
	if ( findOpenedEnvironment(row,col,name,view) )
	{
		view->getDoc()->insertText( row,col, "\\end{"+name+"}" );
// 		view->setCursorPositionReal(row+1,0);
	}
}

//////////////////// insert newlines inside an environment ////////////////////

// intelligent newlines: look for the last opened environment
// and decide what to insert

void EditorExtension::insertIntelligentNewline(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	uint row,col;
	QString name;

	if ( findOpenedEnvironment(row,col,name,view) )
	{
		if ( isListEnvironment(name) )
		{
			view->getDoc()->insertText( row,col,"\n\\item " );
			view->setCursorPositionReal(row + 1,6);
		}
// 		else if ( isTabEnvironment(name) || isMathEnvironment(name) )
// 		{
// 			view->getDoc()->insertText( row,col,"\\\\\n" );
// 			view->setCursorPositionReal(row + 1,0);
// 		}
		else
			view->getDoc()->insertText( row,col,"\\\\\n" );
	}
	else
		view->getDoc()->insertText( row,col,"\\\\\n" );

}

bool EditorExtension::findOpenedEnvironment(uint &row,uint &col, QString &envname, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return false;
	
	// get current cursor position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	EnvData env;
	uint startrow = row;
	uint startcol = col;
	
	//kdDebug() << "   close - start " << endl;
	// accept a starting place outside an environment
	bool env_position = isEnvironmentPosition(doc,row,col,env);
	
	// We can also accept a column, if we are on the left side of an environment.
	// But we should decrease the current cursor position for the search.
	if ( env_position && env.cpos!=EnvInside )
	{
		if ( env.cpos==EnvLeft && !decreaseCursorPosition(doc,startrow,startcol) )
		return false;
		env_position = false;
	}
	
	if ( !env_position && findEnvironmentTag(doc,startrow,startcol,env,true) )
	{
		//kdDebug() << "   close - found begin env at:  " << env.row << "/" << env.col << " " << env.name << endl;
		envname = env.name;
		return true;
	}
	else
		return false;
}

//////////////////// select an environment  ////////////////////

void EditorExtension::selectEnvironment(bool inside, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	EnvData envbegin,envend;
	
	if ( !view->getDoc()->hasSelection() || !expandSelectionEnvironment(inside,view) ) 
	{
		if ( getEnvironment(inside,envbegin,envend,view) )
			view->getDoc()->setSelection(envbegin.row,envbegin.col,envend.row,envend.col);
	}
}

void EditorExtension::deleteEnvironment(bool inside, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	EnvData envbegin,envend;
	
	if ( getEnvironment(inside,envbegin,envend,view) )
	{
		Kate::Document *doc = view->getDoc();
		doc->clearSelection();
		doc->removeText(envbegin.row,envbegin.col,envend.row,envend.col);
		view->setCursorPosition(envbegin.row,0);
	}
}

// calculate start and end of an environment

bool EditorExtension::getEnvironment(bool inside, EnvData &envbegin, EnvData &envend, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return false;
	
	uint row,col;
	
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	if ( !findBeginEnvironment(doc,row,col,envbegin) )
		return false;
	if ( !findEndEnvironment(doc,row,col,envend) )
		return false;
	
	if ( inside )
	{
		// check first line
		envbegin.col += envbegin.len;
		if ( envbegin.col >= (uint)doc->lineLength(envbegin.row) )
		{
		++envbegin.row;
		envbegin.col = 0;
		}
	}
	else
	{
		envend.col += envend.len;
		// check last line
		if ( envbegin.col==0 && envend.col==(uint)doc->lineLength(envend.row) )
		{
		++envend.row;
		envend.col = 0;
		}
	}
	
	return true;
}

// determine text, startrow and startcol of current environment

QString EditorExtension::getEnvironmentText(int &row, int &col, QString &name, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return QString::null;

	EnvData envbegin,envend;
	
	if ( getEnvironment(false,envbegin,envend,view) && envbegin.name!="document" ) 
	{
		row = envbegin.row;
		col = envbegin.col;
		name = envbegin.name;
		return view->getDoc()->text(envbegin.row,envbegin.col,envend.row,envend.col);
	}
	else
	{
		return QString::null;
	}
}

// when an environment is selected (inside or outside), 
// the selection is expanded to the surrounding environment

bool EditorExtension::expandSelectionEnvironment(bool inside, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return false;
	
	Kate::Document *doc = view->getDoc();
	if ( ! doc->hasSelection() )
		return false;
		
	// get current position
	uint row,col;
	view->cursorPositionReal(&row,&col);
	
	// get current selection
	uint row1 = doc->selStartLine();
	uint col1 = doc->selStartCol();
	uint row2 = doc->selEndLine();
	uint col2 = doc->selEndCol();

	// determine current environment outside 
	EnvData oenvbegin,oenvend;
	if ( ! getEnvironment(false,oenvbegin,oenvend,view)  )
		return false;
	
	bool newselection = false;
	// first look, if this environment is selected outside
	if ( row1==oenvbegin.row && col1==oenvbegin.col && row2==oenvend.row && col2==oenvend.col ) {
		if ( ! decreaseCursorPosition(doc,oenvbegin.row,oenvbegin.col) )
			return newselection;
		view->setCursorPositionReal(oenvbegin.row,oenvbegin.col);
		// search the surrounding environment and select it
		if ( getEnvironment(inside,oenvbegin,oenvend,view) ) {
			doc->setSelection(oenvbegin.row,oenvbegin.col,oenvend.row,oenvend.col);
			newselection = true;
		}
	} else { 
		// then determine current environment inside 
		EnvData ienvbegin,ienvend;
		getEnvironment(true,ienvbegin,ienvend,view);
		// and look, if this environment is selected inside
		if ( row1==ienvbegin.row && col1==ienvbegin.col && row2==ienvend.row && col2==ienvend.col ) {
			if ( ! decreaseCursorPosition(doc,oenvbegin.row,oenvbegin.col) )
				return newselection;
			view->setCursorPositionReal(oenvbegin.row,oenvbegin.col);
			// search the surrounding environment and select it
			if ( getEnvironment(inside,ienvbegin,ienvend,view) ) {
				doc->setSelection(ienvbegin.row,ienvbegin.col,ienvend.row,ienvend.col);
				newselection = true;
			}
		}
	} 
	
	// restore old cursor position
	view->setCursorPositionReal(row,col);
	return newselection;
}

//////////////////// search for \begin{env}  ////////////////////

// Find the last \begin{env} tag. If the current cursor is over
//  - \begin{env} tag: we will stop immediately
//  - \end{env} tag: we will start before this tag

bool EditorExtension::findBeginEnvironment(Kate::Document *doc, uint row, uint col,EnvData &env)
{
	// kdDebug() << "   find begin:  " << endl;
	if ( isEnvironmentPosition(doc,row,col,env) )
	{
		// already found position?
		//kdDebug() << "   found env at:  " << env.row << "/" << env.col << " " << env.name << endl;
		if ( env.tag == EnvBegin )
		{
		//kdDebug() << "   is begin env at:  " << env.row << "/" << env.col << " " << env.name << endl;
		return true;
		}
	
		// go one position back
		//kdDebug() << "   is end env at:  " << env.row << "/" << env.col << " " << env.name << endl;
		row = env.row;
		col = env.col;
		if ( ! decreaseCursorPosition(doc,row,col) )
		return false;
	}
	
	// looking back for last environment
	//kdDebug() << "   looking back from pos:  " << row << "/" << col << " " << env.name << endl;
	return findEnvironmentTag(doc,row,col,env,true);
}

//////////////////// search for \end{env}  ////////////////////

// Find the last \begin{env} tag. If the current cursor is over
//  - \end{env} tag: we will stop immediately
//  - \begin{env} tag: we will start behind this tag

bool EditorExtension::findEndEnvironment(Kate::Document *doc, uint row, uint col,EnvData &env)
{
	if ( isEnvironmentPosition(doc,row,col,env) )
	{
		// already found position?
		if ( env.tag == EnvEnd )
			return true;

		// go one position forward
		row = env.row;
		col = env.col + 1;
	}

	// looking forward for the next environment
	return findEnvironmentTag(doc,row,col,env,false);
}

//////////////////// search for an environment tag ////////////////////

// find the last/next non-nested environment tag

bool EditorExtension::findEnvironmentTag(Kate::Document *doc, uint row, uint col,
                                  EnvData &env,bool backwards)
{
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);
	//QRegExp reg("\\\\(begin|end)\\s*\\{\\s*([A-Za-z]+\\*?)\\s*\\}");
	
	uint envcount = 0;
	QString wrong_env = ( backwards ) ? "end" : "begin";
	while ( iface->searchText(row,col,m_reg,&env.row,&env.col,&env.len,backwards) )
	{
		//   kdDebug() << "   iface " << env.row << "/" << env.col << endl;
		if ( isValidBackslash(doc,env.row,env.col) )
		{
			if ( m_reg.cap(1) == wrong_env )
			{
				++envcount;
			}
			else
			{
				if ( envcount > 0 )
					--envcount;
				else
				{
					env.name = m_reg.cap(2);
					return true;
				}
			}
		}
	
		// new start position
		if ( !backwards )
		{
			row = env.row;
			col = env.col + 1;
		}
		else
		{
			row = env.row;
			col = env.col;
			if ( ! decreaseCursorPosition(doc,row,col) )
				return false;
		}
	}

	return false;
}

//////////////////// check for an environment position ////////////////////

// Check if the current position belongs to an environment. The result is set
// to the beginning backslash of the environment tag. The same algorithms as
// matching brackets is used.

bool EditorExtension::isEnvironmentPosition(Kate::Document *doc, uint row, uint col, EnvData &env)
{
	// get real textline without comments, quoted characters and pairs of backslashes
	QString textline = getTextLineReal(doc,row);
	
	if ( col > textline.length() )
		return false;
	
	EnvData envright;
	bool left = false;
	bool right = false;
	
	KTextEditor::SearchInterface *iface;
	iface = dynamic_cast<KTextEditor::SearchInterface *>(doc);
	
	// check if there is a match in this line from the current position to the left
	int startcol = ( textline[col] == '\\' ) ? col - 1 : col;
	if ( startcol > 6 )
	{
		int pos = textline.findRev(m_reg,startcol);
		env.len = m_reg.matchedLength();
		//kdDebug() << "   is - search to left:  pos=" << pos << " col=" << col << endl;
		if ( pos!=-1 && (uint)pos<col && col<=(uint)pos+env.len )
		{
			env.row = row;
			env.col = pos;
			env.tag = ( textline.at(pos+1) == 'b' ) ? EnvBegin : EnvEnd;
			env.name = m_reg.cap(2);
			env.cpos =  ( col < (uint)pos+env.len ) ? EnvInside : EnvRight;
			// we have already found a tag, if the cursor is inside, but not behind this tag
			if ( env.cpos == EnvInside )
				return true;
			left = true;
		//kdDebug() << "   is - found left:  pos=" << pos << " " << env.name << " " << QString(textline.at(pos+1)) << endl;
		}
	}
	
	// check if there is a match in this line from the current position to the right
	if ( textline[col]=='\\' && col==(uint)textline.find(m_reg,col) )
	{
		envright.row = row;
		envright.col = col;
		envright.len = m_reg.matchedLength();
		envright.tag = ( textline.at(col+1) == 'b' ) ? EnvBegin : EnvEnd;
		envright.name = m_reg.cap(2);
		envright.cpos = EnvLeft;
		right = true;
		//kdDebug() << "   is - found right:  pos=" <<col << " " << envright.name << " " << QString(textline.at(col+1)) << endl;
	}
	
	// did we find a tag?
	if ( ! (left || right) )
		return false;
	
	// now check, which tag we should be taken (algorithm like matching brackets)
	
	if ( m_overwritemode )
	{
		if ( right && envright.tag==EnvBegin )
		{
		env = envright;
		return true;
		}
		else if ( left && env.tag==EnvEnd )
		return true;
		else
		return false;
	}
	else if ( left && env.tag==EnvEnd )
	{
		//kdDebug() << "   1: accept left end:  " << env.name << endl;
		return true;
	}
	else if ( right && envright.tag==EnvBegin )
	{
		//kdDebug() << "   2: accept right begin:  " << envright.name << endl;
		env = envright;
	}
	else if ( left && env.tag==EnvBegin )
	{
		// kdDebug() << "   3: accept left begin:  " << env.name << endl;
		return true;
	}
	else if ( right && envright.tag==EnvEnd )
	{
		//kdDebug() << "   4: accept right end:  " << envright.name << endl;
		env = envright;
	}
	else
		return false;
	
	return true;
}

//////////////////// check for a comment ////////////////////

// check if the current position is within a comment

bool EditorExtension::isCommentPosition(Kate::Document *doc, uint row, uint col)
{
	QString textline = doc->textLine(row);
	
	bool backslash = false;
	for ( uint i=0; i<col; ++i )
	{
		if ( textline[i] == '%' )
		{
		if ( !backslash )
			return true;                  // found a comment sign
		else
			backslash = false;
		}
		else if ( textline[i] == '\\' )   // count number of backslashes
		backslash = !backslash;
		else
		backslash = false;               // no backslash
	}
	
	return false;
}

// check if the character at text[col] is a valid backslash:
//  - there is no comment sign in this line before
//  - there is not a odd number of backslashes directly before

bool EditorExtension::isValidBackslash(Kate::Document *doc, uint row, uint col)
{
	QString textline = doc->textLine(row);
	
	bool backslash = false;
	for ( uint i=0; i<col; ++i )
	{
		if ( textline[i] == '%' )
		{
		if ( !backslash )
			return false;                 // found a comment sign
		else
			backslash = false;
		}
		else if ( textline[i] == '\\' )   // count number of backslashes
		backslash = !backslash;
		else
		backslash = false;               // no backslash
	}
	
	return !backslash;
}

//////////////////// goto next bullet ////////////////////

void EditorExtension::gotoBullet(bool backwards, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col,ypos,xpos,len;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	// change the start position or we will stay at this place
	if ( backwards )
	{
		if ( ! decreaseCursorPosition(doc,row,col) )
		return;
	}
	else
	{
		if ( ! increaseCursorPosition(doc,row,col) )
		return;
	}
	
	if ( doc->searchText(row,col,s_bullet,&ypos,&xpos,&len,true,backwards) )
	{
		doc->setSelection(ypos,xpos,ypos,xpos+1);
		view->setCursorPositionReal(ypos,xpos);
	}
}

//////////////////// increase/decrease cursor position ////////////////////

bool EditorExtension::increaseCursorPosition(Kate::Document *doc, uint &row, uint &col)
{
	bool ok = true;
	
	if ( (int)col < doc->lineLength(row)-1 )
		++col;
	else if ( row < doc->numLines() - 1 )
	{
		++row;
		col=0;
	}
	else
		ok = false;
	
	return ok;
}

bool EditorExtension::decreaseCursorPosition(Kate::Document *doc, uint &row, uint &col)
{
	bool ok = true;
	
	if (col > 0)
		--col;
	else if ( row > 0 )
	{
		--row;
		col = doc->lineLength(row) - 1;
	}
	else
		ok = false;
	
	return ok;
}

//////////////////// texgroups ////////////////////

// goto the next non-nested bracket

void EditorExtension::gotoTexgroup(bool backwards, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	bool found;
	BracketData bracket;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	m_overwritemode = view->isOverwriteMode();
	
	// start searching
	if ( backwards )
		found = findOpenBracket(doc,row,col,bracket);
	else
	{
		found = findCloseBracket(doc,row,col,bracket);
		// go behind the bracket
		if ( ! m_overwritemode )
		++bracket.col;
	}
	
	if ( found )
		view->setCursorPositionReal(bracket.row,bracket.col);
}

// match the opposite bracket

void EditorExtension::matchTexgroup(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	BracketData bracket;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	m_overwritemode = view->isOverwriteMode();
	
	// this operation is only allowed at a bracket position
	if ( !isBracketPosition(doc,row,col,bracket) )
		return;
	
	// start searching
	bool found = false;
	if ( bracket.open )
	{
		found = findCloseBracketTag(doc,bracket.row,bracket.col+1,bracket);
		// go behind the bracket
		if ( ! m_overwritemode )
		++bracket.col;
	}
	else
	{
		if ( !decreaseCursorPosition(doc,bracket.row,bracket.col) )
		return;
		found = findOpenBracketTag(doc,bracket.row,bracket.col,bracket);
	}
	
	if ( found )
		view->setCursorPositionReal(bracket.row,bracket.col);
}

//////////////////// close an open texgroup  ////////////////////

// search for the last opened texgroup and close it

void EditorExtension::closeTexgroup(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	uint row,col;
	BracketData bracket;
	
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	uint rowtemp = row;
	uint coltemp = col;
	if ( !decreaseCursorPosition(doc,rowtemp,coltemp) )
		return;
	
	if ( findOpenBracketTag(doc,rowtemp,coltemp,bracket) )
	{
		doc->insertText( row,col,"}" );
		view->setCursorPositionReal(row,col+1);
	}
}

//////////////////// select a texgroup  ////////////////////

void EditorExtension::selectTexgroup(bool inside, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	BracketData open,close;
	
	if ( getTexgroup(inside,open,close,view) )
	{
		Kate::Document *doc = view->getDoc();
		doc->setSelection(open.row,open.col,close.row,close.col);
	}
}

void EditorExtension::deleteTexgroup(bool inside, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	BracketData open,close;
	
	if ( getTexgroup(inside,open,close,view) )
	{
		Kate::Document *doc = view->getDoc();
		doc->clearSelection();
		doc->removeText(open.row,open.col,close.row,close.col);
		view->setCursorPositionReal(open.row,open.col+1);
	}
}

// calculate start and end of an environment

bool EditorExtension::getTexgroup(bool inside, BracketData &open, BracketData &close, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return false;
	
	uint row,col;
	
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	if ( !findOpenBracket(doc,row,col,open) ) { kdDebug() << "no open bracket" << endl; return false;}
	if ( !findCloseBracket(doc,row,col,close) ) { kdDebug() << "no close bracket" << endl; return false;}
	
	if ( inside )
		++open.col;
	else
		++close.col;

	return true;
}

//////////////////// search for a bracket position  ////////////////////

// Find the last opening bracket. If the current cursor is over
//  - '{': we will stop immediately
//  - '}': we will start before this character

bool EditorExtension::findOpenBracket(Kate::Document *doc, uint row, uint col, BracketData &bracket)
{
	if ( isBracketPosition(doc,row,col,bracket) )
	{
		// already found position?
		if ( bracket.open ) return true;
		
		
		// go one position back
		row = bracket.row;
		col = bracket.col;
		if ( ! decreaseCursorPosition(doc,row,col) ) return false;
	}
	
	// looking back for last bracket
	return findOpenBracketTag(doc,row,col,bracket);
}

// Find the last closing bracket. If the current cursor is over
//  - '}': we will stop immediately
//  - '{': we will start behind this character

bool EditorExtension::findCloseBracket(Kate::Document *doc, uint row, uint col, BracketData &bracket)
{
	if ( isBracketPosition(doc,row,col,bracket) )
	{
		// already found position?
		if ( ! bracket.open ) return true;
	
		// go one position forward
		row = bracket.row;
		col = bracket.col + 1;
	}
	
	// looking forward for next bracket
	return findCloseBracketTag(doc,row,col,bracket);
}

/*
   Bracket matching uses the following algorithm (taken from Kate):
   1) If in overwrite mode, match the bracket currently underneath the cursor.
   2) Otherwise, if the character to the left of the cursor is an ending bracket,
      match it.
   3) Otherwise if the character to the right of the cursor is a
      starting bracket, match it.
   4) Otherwise, if the the character to the left of the cursor is an
      starting bracket, match it.
   5) Otherwise, if the character to the right of the cursor is an
      ending bracket, match it.
   6) Otherwise, don't match anything.
*/

bool EditorExtension::isBracketPosition(Kate::Document *doc, uint row, uint col, BracketData &bracket)
{
	// default results
	bracket.row = row;
	bracket.col = col;
	
	QString textline = getTextLineReal(doc,row);
	QChar right = textline[col];
	QChar left  = ( col > 0 ) ? textline[col-1] : QChar(' ');
	
	kdDebug() << QString("isBracketPosition: (%1,%2) left %3 right %4").arg(row).arg(col).arg(left).arg(right) << endl;
	if ( m_overwritemode )
	{
		if ( right == '{' )
		{
		bracket.open = true;
		}
		else if ( left == '}' )
		{
		bracket.open = false;
		}
		else
		return false;
	}
	else if ( left == '}' )
	{
		bracket.open = false;
		--bracket.col;
	}
	else if ( right == '{' )
	{
		bracket.open = true;
	}
	else if ( left == '{' )
	{
		bracket.open = true;
		--bracket.col;
	}
	else if ( right == '}' )
	{
		bracket.open = false;
	}
	else
		return false;
	
	return true;
}

// find next non-nested closing bracket

bool EditorExtension::findCloseBracketTag(Kate::Document *doc, uint row, uint col,BracketData &bracket)
{
	uint brackets = 0;
	for ( uint line=row; line<doc->numLines(); ++line )
	{
		uint start = ( line == row ) ? col : 0;
		QString textline = getTextLineReal(doc,line);
		for ( uint i=start; i<textline.length(); ++i )
		{
		if ( textline[i] == '{' )
		{
			++brackets;
		}
		else if ( textline[i] == '}' )
		{
			if ( brackets > 0 )
				--brackets;
			else
			{
			bracket.row = line;
			bracket.col = i;
			bracket.open = false;
			return true;
			}
		}
		}
	}
	
	return false;
}

// find next non-nested opening bracket

bool EditorExtension::findOpenBracketTag(Kate::Document *doc, uint row, uint col, BracketData &bracket)
{
	uint brackets = 0;
	for ( int line=row; line>=0; --line )
	{
		QString textline = getTextLineReal(doc,line);
		int start = ( line == (int)row ) ? col : textline.length()-1;
		for ( int i=start; i>=0; --i )
		{
			kdDebug() << "findOpenBracketTag: (" << line << "," << i << ") = " << textline[i].latin1() << endl;
			if ( textline[i] == '{' )
			{
				if ( brackets > 0 )
					--brackets;
				else
				{
					bracket.row = line;
					bracket.col = i;
					bracket.open = true;
					return true;
				}
			}
			else if ( textline[i] == '}' )
			{
				++brackets;
			}
		}
	}
	
	kdDebug() << "nothting found" << endl;
	return false;
}

//////////////////// get real text ////////////////////

// get current textline and remove
//  - all pairs of backslashes: '\\'
//  - all quoted comment signs: '\%'
//  - all quoted brackets: '\{' and '\}'
//  - all comments
// replace these characters one one, which never will be looked for

QString EditorExtension::getTextLineReal(Kate::Document *doc, uint row)
{
	QString textline = doc->textLine(row);
	uint len = textline.length();
	if ( len == 0)
		return QString::null;
	
	bool backslash = false;
	for (uint i=0; i<len; ++i )
	{
		if ( textline[i]=='{' ||textline[i]=='}' )
		{
		if ( backslash )
		{
			textline[i-1] = '&';
			textline[i] = '&';
		}
		backslash = false;
		}
		else if ( textline[i]=='\\' )
		{
		if ( backslash )
		{
			textline[i-1] = '&';
			textline[i] = '&';
			backslash = false;
		}
		else
			backslash = true;
		}
		else if ( textline[i]=='%' )
		{
		if ( backslash )
		{
			textline[i-1] = '&';
			textline[i] = '&';
		}
		else
		{
			len = i;
			break;
		}
		backslash = false;
		}
		else
		backslash = false;
	
	}
	
	// return real text
	return textline.left(len);
}

//////////////////// capture the current word ////////////////////

// Capture the current word from the cursor position to the left and right.
// The result depens on the given search mode;
// - smTex       only letters, except backslash as first and star as last  character
// - smLetter:   only letters
// - smWord:     letters and digits
// - smNospace:  everything except white space

bool EditorExtension::getCurrentWord(Kate::Document *doc, uint row, uint col, EditorExtension::SelectMode mode, QString &word,uint &x1,uint &x2)
{
    // get real textline without comments, quoted characters and pairs of backslashes
	QString textline = getTextLineReal(doc,row);
	if ( col > textline.length() )
		return false;
	
	QRegExp reg;
	QString pattern1,pattern2;
	switch ( mode )
	{
	case smLetter :
		pattern1 = "[^a-zA-Z]+";
		pattern2 = "[a-zA-Z]+";
		break;
	case smWord   :
		pattern1 = "[^a-zA-Z0-9]";
		pattern2 = "[a-zA-Z0-9]+";
		break;
	case smNospace:
		pattern1 = "\\s";
		pattern2 = "\\S+";
		break;
	default       :
		pattern1 = "[^a-zA-Z]";
		pattern2 = "\\\\?[a-zA-Z]+\\*?";
		break;
	}
	x1 = x2 = col;
	
	int pos;
	// search to the left side
	if ( col > 0 )
	{
		reg.setPattern(pattern1);
		pos = textline.findRev(reg,col-1);
		if ( pos != -1 ) {        // found an illegal character
			x1 = pos + 1;
			if ( mode == smTex ) {
				if ( textline[pos] == '\\' )
					x1 = pos;
				col = x1;
			}
		} else {
			x1 = 0;               // pattern matches from beginning of line
		} 
	}
	
	// search at the current position
	reg.setPattern(pattern2);
	pos = textline.find(reg,col);
	if ( pos!=-1 && (uint)pos==col )
	{
		x2 = pos + reg.matchedLength();
	}
	
	// get all characters
	if ( x1 != x2 )
	{
		word = textline.mid(x1,x2-x1);
		return true;
	}
	else
		return false;
}


//////////////////// move/unmove selections ////////////////////

void EditorExtension::commentSelection(bool insert, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	moveSelection("%",insert,view);
}

void EditorExtension::spaceSelection(bool insert, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	moveSelection(" ",insert,view);
}

void EditorExtension::tabSelection(bool insert, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	moveSelection("\t",insert,view);
}

void EditorExtension::stringSelection(bool insert, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	bool ok;
	QString text = KInputDialog::getText(i18n("Insert Text"), i18n("Please enter the text to insert:"),"", &ok, view);
	if ( ok && (!text.isEmpty()))
	{
		moveSelection(text, insert, view);
	}
}

void EditorExtension::moveSelection(const QString &prefix,bool insertmode, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	// get current position
	uint row,col,row1,col1,row2,col2;
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	bool blockselectionmode = doc->blockSelectionMode();
	
	if ( doc->hasSelection() )
	{             // selection: all lines in the selected range
		row1 = doc->selStartLine();
		col1 = doc->selStartCol();
		row2 = doc->selEndLine();
		col2 = doc->selEndCol();
		if ( !blockselectionmode )
		{
		if ( col1 >= doc->textLine(row1).length() )
			(void) increaseCursorPosition(doc,row1,col1);
		if ( col2 == 0 )
			(void) decreaseCursorPosition(doc,row2,col2);
		}
	}
	else
	{                                // no selection: only one line
		row1 = row2 = row;
		col1 = col2 = col;
	}
	//    kdDebug() << "   selection:  " << row1 << "/" << col1 << " " << row2 << "/" << col2 << endl;
	
	// we always start at column 0, if not in blockselection mode
	uint startcol = ( blockselectionmode ) ? col1 : 0;
	uint prefixlen = prefix.length();
	
	int changecol  = 0;
	int changecol1 = 0;
	int changecol2 = 0;
	int change = ( insertmode ) ? prefixlen : -prefixlen;
	for (uint line=row1; line<=row2; ++line)
	{
		bool action = true;
		if ( insertmode )
		doc->insertText(line,startcol,prefix);
		else if ( doc->textLine(line).mid(startcol,prefixlen) == prefix )
		doc->removeText(line,startcol,line,startcol+prefixlen);
		else
		action = false;
	
		// calculate changes of selection range and cursor position
		if ( action )
		{
		if ( line == row )
			changecol = change;
		if ( line == row1 )
			changecol1 = change;
		if ( line == row2 )
			changecol2 = change;
		}
	}
	
	// also move the selection and the cursor position
	if ( doc->hasSelection() )
	{
		if ( blockselectionmode )
		doc->setSelection(row1,col1,row2,col2+change);
		else
		{
		if ( col1 > 0 )
			col1 += changecol1;
		if ( row > row2 )
		{
			++row2;
			col2 = 0;
		}
		else
			col2 += changecol2;
		doc->setSelection(row1,col1,row2,col2);
		}
	}
	
	// finally update cursor position
	view->setCursorPositionReal(row,col+changecol);
}

//////////////////// paragraph ////////////////////

void EditorExtension::selectParagraph(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	uint startline,endline;
	
	if ( findCurrentTexParagraph(startline,endline,view) )
	{
		view->getDoc()->setSelection(startline,0,endline+1,0);
	}
}

void EditorExtension::deleteParagraph(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;

	uint startline,endline;
	
	if ( findCurrentTexParagraph(startline,endline,view) )
	{
		Kate::Document *doc = view->getDoc();
		doc->clearSelection();
		if ( startline > 0 )
		--startline;
		else if ( endline < doc->numLines()-1 )
		++endline;
		doc->removeText(startline,0,endline+1,0);
		view->setCursorPosition(startline,0);
	}
}

// get the range of the current paragraph

bool EditorExtension::findCurrentTexParagraph(uint &startline, uint &endline, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return false;
	
	uint row,col;
	
	// get current position
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	// don't accept an empty line as part of a paragraph
	if ( doc->textLine(row).stripWhiteSpace().isEmpty() )
		return false;
	
	// settings default results
	startline = row;
	endline = row;
	
	// find the previous empty line
	for ( int line=row-1; line>=0; --line )
	{
		if ( doc->textLine(line).stripWhiteSpace().isEmpty() )
		break;
		startline = line;
	}
	
	// find the next empty line
	for ( uint line=row+1; line<doc->numLines(); ++line )
	{
		if ( doc->textLine(line).stripWhiteSpace().isEmpty() )
		break;
		endline = line;
	}
	
	// settings result
	return true;
}

//////////////////// one line of text////////////////////

void EditorExtension::selectLine(Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	// get current position
	uint row,col;
	QString word;
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	if ( doc->lineLength(row) > 0 )
	{
		doc->setSelection(row,0,row+1,0);
	}
}

//////////////////// LaTeX command ////////////////////

void EditorExtension::selectWord(EditorExtension::SelectMode mode, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	// get current position
	uint row,col,col1,col2;
	QString word;
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	if ( getCurrentWord(doc,row,col,mode,word,col1,col2) )
	{
		doc->setSelection(row,col1,row,col2);
	}
}

void EditorExtension::deleteWord(EditorExtension::SelectMode mode, Kate::View *view)
{
	view = determineView(view);
	if ( !view ) return;
	
	// get current position
	uint row,col,col1,col2;
	QString word;
	Kate::Document *doc = view->getDoc();
	view->cursorPositionReal(&row,&col);
	
	if ( getCurrentWord(doc,row,col,mode,word,col1,col2) )
	{
		doc->removeText(row,col1,row,col2);
	}
}

void EditorExtension::nextBullet()
{
	gotoBullet(false);
}

void EditorExtension::prevBullet()
{
	gotoBullet(true);
}

void EditorExtension::completeWord()
{
	complete()->editComplete(m_ki->viewManager()->currentView(), KileDocument::CodeCompletion::cmLatex);
}

void EditorExtension::completeEnvironment()
{
	complete()->editComplete(m_ki->viewManager()->currentView(), KileDocument::CodeCompletion::cmEnvironment);
}

void EditorExtension::completeAbbreviation()
{
	complete()->editComplete(m_ki->viewManager()->currentView(), KileDocument::CodeCompletion::cmAbbreviation);
}

}

#include "kileedit.moc"
