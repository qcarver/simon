//
// C++ Implementation: addwordrecordpage
//
// Description: 
//
//
// Author: Peter Grasch <bedahr@gmx.net>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "addwordrecordpage.h"

/**
 * \brief Constructor - also creates the GUI Elements
 * \author Peter Grasch
 * @param parent
 * The parent of the widget
 */
AddWordRecordPage::AddWordRecordPage(QWidget *parent)
 : QWizardPage(parent)
{
	setTitle("Aufnehmen des Wortes");
	QVBoxLayout *lay = new QVBoxLayout(this);
	QLabel *desc = new QLabel(this);
	desc->setText(tr("Bitte nehmen Sie nun das hinzuzufuegende Wort zweimal auf.\n\nBitte achten Sie darauf, das Wort deutlich, aber natuerlich\nauszusprechen und vermeiden Sie Hintergrundgeraeusche.\n"));
	rec1 = new RecWidget(tr("Aufnahme 1"), "1.wav", this);
	rec2 = new RecWidget(tr("Aufnahme 2"), "2.wav", this);
	lay->addWidget(desc);
	lay->addWidget(rec1);
	lay->addWidget(rec2);
	
	connect(rec1, SIGNAL(recordingFinished()), this, SIGNAL(completeChanged()));
	connect(rec2, SIGNAL(recordingFinished()), this, SIGNAL(completeChanged()));
	connect(rec1, SIGNAL(sampleDeleted()), this, SIGNAL(completeChanged()));
	connect(rec2, SIGNAL(sampleDeleted()), this, SIGNAL(completeChanged()));
}

/**
 * \brief Cleans up and removes the samples
 * \author Peter Grasch
 */
void AddWordRecordPage::cleanUp()
{
	if (rec1->hasRecordingReady())
		rec1->deleteSample();
	if (rec2->hasRecordingReady())
		rec2->deleteSample();
}


/**
 * \brief Destructor; Calls cleanUp()
 * \author Peter Grasch
 * \see cleanUp()
 */
AddWordRecordPage::~AddWordRecordPage()
{
	cleanUp();
}


