/***************************************************************************
                          latexoutputfilter.h  -  description
                             -------------------
    begin                : Die Sep 16 2003
    copyright            : (C) 2003 by Jeroen Wijnhout
    email                : wijnhout@science.uva.nl
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef LATEXOUTPUTFILTER_H
#define LATEXOUTPUTFILTER_H

#include "outputfilter.h"
#include "latexoutputinfo.h"
#include <qvaluestack.h>
#include <qstring.h>
#include <qwidget.h>

/**An object of this class is used to parse the output messages
generated by a TeX/LaTeX-compiler.

@author Thorsten L�ck
  *@author Jeroen Wijnhout
  */

class LatexOutputFilter : public OutputFilter
{
    public:
        LatexOutputFilter(LatexOutputInfoArray* LatexOutputInfoArray,
            MessageWidget *LogWidget = NULL,
            MessageWidget *OutputWidget = NULL );
        ~LatexOutputFilter();

        int GetNumberOfOutputPages() const;
        virtual unsigned int Run(QString logfile);

    protected:
        /**
        Parses the given line for the start of new files or the end of
        old files.
        */
        void UpdateFileStack(const QString &strLine);

        /**
        Forwards the currently parsed item to the item list.
        */
        void FlushCurrentItem();

        // overridings
    public:
        virtual QString GetResultString();
        /** Return number of errors etc. found in log-file. */
        void GetErrorCount(int *errors, int *warnings, int *badboxes);

    protected:
        virtual bool OnPreCreate();
        virtual short ParseLine(QString strLine, short dwCookie);
        virtual bool OnTerminate();

    private:

        // types
    protected:
        /**
        These constants are describing, which item types is currently
        parsed.
        */
        enum tagCookies
        {
            itmNone = 0,
            itmError,
            itmWarning,
            itmBadBox
        };

        // attributes
    public:
        /** number or errors detected */
        int m_nErrors;

        /** number of warning detected */
        int m_nWarnings;

        /** number of bad boxes detected */
        int m_nBadBoxes;

    private:
        /** number of output pages detected */
        int m_nOutputPages;

        /**
        Stack containing the files parsed by the compiler. The top-most
        element is the currently parsed file.
        */
        QValueStack<QString> m_stackFile;

        /** The item currently parsed. */
        LatexOutputInfo m_currentItem;

        /** True, if a filename in the TeX-Output spreads over more than one line */
        bool m_bFileNameOverLines;

        /**
        The first part of a filename, if the whole filename
        spreads over more than one line.
        */
        QString m_strPartialFileName;

    public:                                                                     // Public attributes
        /** Pointer to list of Latex output information */
        LatexOutputInfoArray *m_InfoList;
        /**  */
        MessageWidget* LogWidget;

        /** for debug reason only */
        bool found;
};
#endif
