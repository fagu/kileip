/**************************************************************************
*   Copyright (C) 2007 by Michel Ludwig (michel.ludwig@kdemail.net)       *
***************************************************************************/

/**************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "toolconfigwidget.h"

ToolConfigWidget::ToolConfigWidget(QWidget *parent) : QWidget(parent)
{
	setupUi(this);
}

ToolConfigWidget::~ToolConfigWidget()
{
}

#include "toolconfigwidget.moc"