//
// C++ Implementation: mergeterminalsselectterminalspage
//
// Description: 
//
//
// Author: Peter Grasch <bedahr@gmx.net>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "mergeterminalsselectterminalspage.h"
#include "wordlistmanager.h"

MergeTerminalsSelectTerminalsPage::MergeTerminalsSelectTerminalsPage(WordListManager *wordListManager, QWidget* parent): QWizardPage(parent)
{
	ui.setupUi(this);
	this->wordListManager = wordListManager;
	setTitle(tr("Terminale ausw�hlen"));
	registerField("newName*", ui.leNewTerminal);
	registerField("terminalA*", ui.lwA, "currentText", SIGNAL(currentTextChanged(QString)));
	registerField("terminalB*", ui.lwB, "currentText", SIGNAL(currentTextChanged(QString)));
	registerField("includeShadow", ui.cbIncludeShadow);
	registerField("includeGrammar", ui.cbIncludeGrammar);
}

void MergeTerminalsSelectTerminalsPage::initializePage()
{
	QStringList availableTerminals;
	availableTerminals = wordListManager->getTerminals(true);
	ui.lwA->addItems(availableTerminals);
	ui.lwB->addItems(availableTerminals);
}


MergeTerminalsSelectTerminalsPage::~MergeTerminalsSelectTerminalsPage()
{
}


