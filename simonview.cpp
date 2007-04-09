/***************************************************************************
 *   Copyright (C) 2006 by Peter Grasch   *
 *   bedahr@gmx.net   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "simonview.h"

/**
 * @brief Constructor
 *
 * This is the constructor of the main-UI class - the Simon View class.
 * It displays a little splash screen and initializes the member-variables
 *
 * @author Peter Grasch
 * @param Qwidget *parent
 * The parent which is passed on to the QMainWindow initialization - Default: NULL
 * @param Qt::WFlags flags
 * The flags which are also passed on to the QMainWindow constructor - as before: Default: NULL
 * 
*/

SimonView::SimonView(QWidget *parent, Qt::WFlags flags)
{
	this->info = new SimonInfo();
	
	//showing splash
	this->info->showSplash();
	
	this->info->writeToSplash("Loading...");
	
	this->control = new SimonControl();
	this->trayManager = new TrayIconManager();
	
	this->trayManager->createIcon( QIcon( ":/images/tray.png" ), "Simon" );
	
	//Preloads all Dialogs
	this->addWordView = new AddWordView(this);
	this->wordList = new WordListView(this);
	this->runDialog = new RunApplicationView(this);
	this->trainDialog = new TrainingView(this);
	this->settingsDialog = new SettingsView(this);
	
	this->vuMeter = new VuMeter();
	//Disabled for now because it crashes the windows compile
	//if (vuMeter->prepare())
	//	vuMeter->start();
	
	QMainWindow(parent,flags);
	ui.setupUi(this);
	
	ui.frmConnecting->setVisible(false);
	

	//Setting up Signal/Slots
	QObject::connect(vuMeter, SIGNAL(level(int)), this, SLOT(setLevel(int)));
	
	QObject::connect(ui.pbAddWord, SIGNAL(clicked()), this, SLOT(showAddWordDialog()));
	QObject::connect(ui.pbEditWordList, SIGNAL(clicked()), this, SLOT(showWordListDialog()));
	QObject::connect(ui.pbRunProgram, SIGNAL(clicked()), this, SLOT(showRunDialog()));
	QObject::connect(ui.pbTrain, SIGNAL(clicked()), this, SLOT(showTrainDialog()));
	QObject::connect(ui.pbSettings, SIGNAL(clicked()), this, SLOT(showSettingsDialog()));
	QObject::connect(ui.pbHide, SIGNAL(clicked()), this, SLOT(hideSimon()));
	QObject::connect(ui.pbClose, SIGNAL(clicked()), this, SLOT(closeSimon()));
	QObject::connect(this->trayManager, SIGNAL(clicked()), this, SLOT(toggleVisibility()));
	QObject::connect(ui.pbActivision, SIGNAL(clicked()), this, SLOT(toggleActivation()));
	QObject::connect(this->trayManager, SIGNAL(middleClicked()), this, SLOT(toggleActivation()));
	
	connect(ui.pbConnect, SIGNAL(clicked()), this, SLOT(connectToServer()));
	connect(ui.pbCancelConnect, SIGNAL(clicked()), this, SLOT(abortConnecting()));
	
	
	QObject::connect(control, SIGNAL(connected()), this, SLOT(connected()));
	QObject::connect(control, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(control, SIGNAL(connectionError(QString)), this, SLOT(errorConnecting(QString)));
	
	//setting Background
	ui.lbBg->lower();
	ui.lbBg->setPixmap(QPixmap(":/images/bg_clear.png"));
	
	ui.lbLogo->setPixmap(QPixmap(":/images/simon.png"));

	this->info->writeToSplash("Connecting to juliusd...");
	connectToServer();
	
//  	SoundControl *mic = new SoundControl();
//  	mic->initializeMic(2, 44100);
//  	long unsigned int length=0;
//  	char* data = mic->capture(5000, length);
// 	
// 	WAV *w = new WAV("test.wav", 44100);
// 	w->addData(data,length);
// 	w->writeFile("test.wav");
	
	//hiding splash again after loading
	this->info->hideSplash();
}

/**
 * \brief Connects to juliusd and gives appropriate status information about it
 *
 * @author Peter Grasch
 */
void SimonView::connectToServer()
{
	ui.pbConnect->setText("Verbinde...");
	ui.pbConnect->setEnabled(false);
	
	ui.frmConnecting->setVisible(true);
	
	this->control->activateSimon();
	this->control->connect();
}

/**
 * \brief Informs the user that we connected to the server
 *
 * @author Peter Grasch
 */
void SimonView::connected()
{
	ui.pbConnect->setText("Verbindung trennen");
	ui.pbConnect->setEnabled(true);
	disconnect(ui.pbConnect, 0,0,0);
	connect(ui.pbConnect, SIGNAL(clicked()), control, SLOT(disconnect()));
	
	ui.frmConnecting->setVisible(false);
	SimonInfo::showMessage("Verbunden zu Julius", 3000);
	
	this->control->getActivitionState();
	this->representState();
}


/**
 * \brief Informs the user that we have been disconnected from the server
 *
 * @author Peter Grasch
 */
void SimonView::disconnected()
{
	ui.pbConnect->setText("Verbinden");
	ui.pbConnect->setEnabled(true);
	disconnect(ui.pbConnect, 0,0,0);
	connect(ui.pbConnect, SIGNAL(clicked()), this, SLOT(connectToServer()));
	
	SimonInfo::showMessage("Verbindung zu Julius verloren", 4000);
	//we should probably try to reconnect at this point
	
	control->deactivateSimon();
	this->representState();
}


/**
 * \brief We canceled the connecting to the server
 *
 * @author Peter Grasch
 */
void SimonView::abortConnecting()
{
	ui.pbConnect->setText("Verbinden");
	ui.pbConnect->setEnabled(true);
	disconnect(ui.pbConnect, 0,0,0);
	connect(ui.pbConnect, SIGNAL(clicked()), this, SLOT(connectToServer()));
	
	ui.frmConnecting->setVisible(false);
	
	this->control->abortConnecting();
	this->representState();
	
	this->control->deactivateSimon();
	
}

/**
 * \brief Error connecting to the server
 *
 * @author Peter Grasch
 * 
 * \param QString error
 * The error that occured
 */
void SimonView::errorConnecting(QString error)
{
	ui.pbConnect->setText("Verbinden");
	ui.pbConnect->setEnabled(true);
	disconnect(ui.pbConnect, 0,0,0);
	connect(ui.pbConnect, SIGNAL(clicked()), this, SLOT(connectToServer()));
	
	ui.frmConnecting->setVisible(false);
	
	this->control->deactivateSimon();
	this->representState();
	
	QMessageBox::critical(this, "Kritischer Verbindungsfehler", "Die Verbindung zum juliusd Erkennungsd�mon konnte nicht aufgenommen werden.\n\nBitte �berpr�fen Sie Ihre Einstellungen, ihre Netzwerkverbindung und ggf. Ihre Firewall.\n\nDie exakte Fehlermeldung lautete:\n"+error);
}



/**
 * \brief Sets the vumeter to the given level
 *
 * @author Peter Grasch
 */
void SimonView::setLevel(int level)
{
	level = abs(level);
	ui.pbLevel1->setValue(level);
	ui.pbLevel2->setValue(level);
}



/**
 * @brief Shows the Run Dialog
 *
 * @author Peter Grasch
 */
void SimonView::showRunDialog()
{
	this->runDialog->exec();
}


/**
 * @brief Shows a dialog to add a new Word to the model
 *
 * @author Peter Grasch
 */
void SimonView::showAddWordDialog()
{
	this->addWordView->exec();
}

/**
 * @brief Shows a dialog to configure simon
 *
 * @author Peter Grasch
 */
void SimonView::showSettingsDialog()
{
	this->settingsDialog->exec();
}

/**
 * @brief Shows a dialog to train the language model
 *
 * @author Peter Grasch
 */
void SimonView::showTrainDialog()
{
	this->trainDialog->exec();
}

/**
 * @brief Shows a dialog to Control the Laguage Model
 *
 * @author Peter Grasch
 */
void SimonView::showWordListDialog()
{
	this->wordList->exec();
}


/**
 * @brief Hide the main window
 *
 * Hides the main window and sends it to the system tray.
 *
 *	@author Peter Grasch
 * @todo For now this function only handles the main program - the dialogs should be hidden too
 *
*/
void SimonView::hideSimon()
{
	hide();
}


/**
 * @brief Shows the main Window
 *
 * Shows the Main Window after it was hidden at first
 *
 *	@author Peter Grasch
 * @todo For now this function only handles the main program - the dialogs should be shown again too
 *
 */
void SimonView::showSimon()
{
	show();
}


/**
 * @brief Toggles the activation state
 *
 * Tells the Control-Layer to toggle the activation state - according to his
 * response the following (re)actions are taken:
 * 	desaturate the Tray Image as needed
 * 	desaturate the simon logo in the bg
 * 	replaces the text on the label to "Deaktivieren"/"Aktivieren"
 *
 *	@author Peter Grasch
 *
 */
void SimonView::toggleActivation()
{
	this->control->toggleActivition();
	this->representState();
}

/**
 * \brief Make the widgets represent the current state (active/inactive)
 * 
 * \author Peter Grasch
 */
void SimonView::representState()
{
	if (this->control->getActivitionState())
	{
		ui.lbLogo->setPixmap(QPixmap(":/images/simon.png"));
		ui.pbActivision->setText("  &Deaktivieren");
		ui.pbActivision->setIcon(QIcon(":/images/icons/media-playback-pause.svg"));
		this->trayManager->createIcon( QIcon( ":/images/tray.png" ), "Simon" );
		
		if (isHidden()) SimonInfo::showMessage(QString("Simon wurde aktiviert"), 2000);

		repaint();
	} else 
	{
		ui.lbLogo->setPixmap(QPixmap(":/images/simon_d.png"));
		ui.pbActivision->setText("  A&ktivieren");
		ui.pbActivision->setIcon(QIcon(":/images/icons/media-playback-start.svg"));

		if (isHidden()) SimonInfo::showMessage(QString("simon wurde deaktiviert"), 2000);

		this->trayManager->createIcon( QIcon( ":/images/tray_d.png" ), "Simon - Deaktiviert" );
		repaint();
	}
}



/**
 * @brief Trigger Visibility
 *
 * Asks of the current status (hidden/visible) and inverts it
 *
 * @author Peter Grasch
 * @see showSimon(), hideSimon()
 *
 */
void SimonView::toggleVisibility()
{
	if (!(this->isHidden()))
		this->hide();
	else this->show();
}


/**
 * @brief Close the main window
 *
 * Closes the whole program and kills all dialogs associated with it
 *
 * @author Peter Grasch
 * @todo For now this function only handles the main program and the tray icon
 *
*/
void SimonView::closeSimon()
{
	if ((!false /*Confirm shutdown*/) || (QMessageBox::question(this, "Wirklich beenden?", "Ein beenden der Applikation wird die Verbindung zur Erkennung beenden und weder Diktatfunktionen noch andere Kommandos k�nnen mehr benutzt werden.\n\nWollen Sie wirklich beenden?",QMessageBox::Yes|QMessageBox::No,QMessageBox::No) == QMessageBox::Yes))
	{
		close();
		this->~ SimonView();
	}
}



/**
 * @brief Destructor
 *
 *	@author Peter Grasch
*/
SimonView::~SimonView()
{
	this->trayManager->~ TrayIconManager();
	this->control->~ SimonControl();
	this->info->~ SimonInfo();
}


