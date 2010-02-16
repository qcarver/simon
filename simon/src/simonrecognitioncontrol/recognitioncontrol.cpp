/*
 *   Copyright (C) 2008 Peter Grasch <grasch@simon-listens.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2,
 *   or (at your option) any later version, as published by the Free
 *   Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include "recognitioncontrol.h"
#include "recognitionconfiguration.h"

#include <adinstreamer/adinstreamer.h>
#include <simoninfo/simoninfo.h>
#include <simonprotocol/simonprotocol.h>
#include <simonmodelmanagementui/AddWord/addwordview.h>
#include <simonmodelmanagementui/modelmanageruiproxy.h>

#include <simonscenarios/trainingmanager.h>

#include <simonscenarios/languagedescriptioncontainer.h>
#include <simonscenarios/trainingcontainer.h>
#include <simonscenarios/model.h>
#include <simonscenarios/scenario.h>
#include <simonscenarios/scenariomanager.h>

#include <simonprogresstracking/operation.h>
#include <simonrecognitionresult/recognitionresult.h>

#include <stdio.h>

#include <QByteArray>
#include <QSslSocket>
#include <QTimer>
#include <QFile>
#include <QDataStream>
#include <QDateTime>
#include <QStringList>

#include <KMessageBox>
#include <KLocalizedString>
#include <KDebug>
#include <KPasswordDialog>
#include <KStringHandler>

#define advanceStream(x) messageLocker.lock();\
					qint64 currentPos = ((qint64)msg.device()->pos())-((qint64)x);\
					msgByte.remove(0,(int)x);\
					msg.device()->seek(currentPos);\
					messageLocker.unlock();
					
#define checkIfSynchronisationIsAborting() if (synchronisationOperation && synchronisationOperation->isAborting()) \
					{ \
						sendRequest(Simond::AbortSynchronisation); \
						synchronisationDone(); \
						return; \
					}
					
#define checkIfMessageFinished(length)	if ((unsigned) msg.device()->bytesAvailable() < length) \
					{ \
						messageNotYetFinished=true; \
						break; \
					}
					
#define parseLengthHeader()		checkIfMessageFinished(sizeof(qint64)); \
					qint64 length; \
					msg >> length; \
					checkIfMessageFinished(length);

RecognitionControl* RecognitionControl::instance;

/**
 *	@brief Constructor
 *	
 *	Creates the Server and sets it up to listen on the supplied port
 *	Default: 4444
 *	
 *	@author Peter Grasch
 *	@param qint16 port
 *	Port the Server should listen to
 */
RecognitionControl::RecognitionControl(QWidget* parent) : QObject(parent),
	adinStreamer(AdinStreamer::getInstance(this)),
	recognitionReady(false),
	socket(new QSslSocket()),
	synchronisationOperation(0),
	modelCompilationOperation(0),
	timeoutWatcher(new QTimer(this))
{
	connect(adinStreamer, SIGNAL(started()), this, SLOT(streamStarted()));
	connect(adinStreamer, SIGNAL(stopped()), this, SLOT(streamStopped()));
	connect(adinStreamer, SIGNAL(requestingPause()), this, SLOT(pauseRecognition()));

	connect(timeoutWatcher, SIGNAL(timeout()), this, SLOT(timeoutReached()));
			
	connect(socket, SIGNAL(readyRead()), this, SLOT(messageReceived()));
	connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(errorOccured()));
	connect(socket, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(errorOccured()));


	connect(socket, SIGNAL(disconnected()), this, SLOT(slotDisconnected()));
	connect(socket, SIGNAL(disconnected()), this, SIGNAL(disconnected()));
	
	connect(this, SIGNAL(simondSystemError(const QString&)), this, SLOT(disconnectFromServer()));

	connect(ModelManagerUiProxy::getInstance(), SIGNAL(recompileModel()), this, SLOT(askStartSynchronisation()));
}

void RecognitionControl::actOnAutoConnect()
{
	if ( RecognitionConfiguration::juliusdAutoConnect() )
		startConnecting();
}

void RecognitionControl::streamStarted()
{
	kDebug() << "Stream has been started";
	emit recognitionStatusChanged(RecognitionControl::Started);
}

void RecognitionControl::streamStopped()
{
	if (isConnected())
		emit recognitionStatusChanged(RecognitionControl::Ready);
	//else don't emit anything; The stream could have been stopped
	//by us, disconnecting; That doesn't mean that the recognition is ready, tough
}


void RecognitionControl::slotDisconnected()
{
	adinStreamer->stop();
	recognitionReady=false;
	if (synchronisationOperation)
	{
		if (synchronisationOperation->isRunning())
			synchronisationOperation->canceled();
		synchronisationOperation = NULL;
	}
	if (modelCompilationOperation)
	{
		if (modelCompilationOperation->isRunning())
			modelCompilationOperation->canceled();
		modelCompilationOperation = NULL;
	}
}

void RecognitionControl::startConnecting()
{
	if (socket->state() != QAbstractSocket::UnconnectedState)
		disconnectFromServer();

	serverConnectionErrors.clear();
	serverConnectionsToTry = RecognitionConfiguration::juliusdServers();
	
	if (serverConnectionsToTry.count() == 0) return;
	
	connectToNext();
}

void RecognitionControl::connectToNext()
{
	if (serverConnectionsToTry.isEmpty())
	{
		emit connectionError(serverConnectionErrors.join("\n"));
	} else {
		QStringList address = serverConnectionsToTry.takeAt(0).split(":");
		connectTo(address[0], address[1].toInt());
	}
}

/**
 * @brief Connects to a juliusd server
 * 
 * 	Connects the QTcpSocket socket (member) to the Host described
 * 	with server (ip or hostname) (default: 127.0.0.1) and port 
 * 	(default: 4444)
 *
 *	@author Peter Grasch
 *	@param QString server
 *	Serverstring - either an ip or a hostname (default: 127.0.0.1)
 *	@param quint16 port
 *	Port (default: 4444)
 */
void RecognitionControl::connectTo(QString server, quint16 port)
{
	if (socket->state() != QAbstractSocket::UnconnectedState) {
		 socket->abort();
	}
	
	disconnect(socket, SIGNAL(encrypted()), 0, 0);
	disconnect(socket, SIGNAL(connected()), 0, 0);

	if (RecognitionConfiguration::juliusdEncrypted())
	{
		socket->setProtocol(QSsl::TlsV1);
		connect(socket, SIGNAL(encrypted()), this, SLOT(connectedTo()));
		socket->connectToHostEncrypted( server, port );
	} else {
		connect(socket, SIGNAL(connected()), this, SLOT(connectedTo()));
		socket->connectToHost( server, port );
	}
	timeoutWatcher->start(RecognitionConfiguration::juliusdConnectionTimeout());
}

void RecognitionControl::errorOccured()
{
	if (timeoutWatcher->isActive())
		timeoutWatcher->stop();

	QList<QSslError> errors = socket->sslErrors();
	if ((errors.count() == 1) && (errors[0].error() == QSslError::SelfSignedCertificate) && (RecognitionConfiguration::juliusdEncrypted()))
	{
		if (KMessageBox::questionYesNoCancel(0, i18n("The certificate of the remote host is self-signed and thus not trustworthy. "
"\n\nDo you still want to continue?"), i18n("Self-Signed Certificate"))==KMessageBox::Yes)
		{
			socket->ignoreSslErrors();
			return;
		} else {
			socket->abort();
			socket->flush();
			socket->close();
		}
	}
	if (socket->error() != QAbstractSocket::UnknownSocketError)
		serverConnectionErrors << socket->errorString();
	else 
	{ //build ssl error list
		for (int i=0; i < errors.count(); i++)
			serverConnectionErrors << errors[i].errorString();
	}
	connectToNext();
}

/**
 * \brief Returns whether the socket is connected to a valid juliusd server
 * 
 * \author Peter Grasch
 * \return bool
 * True = is connected
 */
bool RecognitionControl::isConnected()
{
	if (!socket) return false;
	
	return (socket->state() == QAbstractSocket::ConnectedState);
}


void RecognitionControl::timeoutReached()
{
	timeoutWatcher->stop();
	serverConnectionErrors << i18n("Request timed out (%1 ms)", RecognitionConfiguration::juliusdConnectionTimeout());
	socket->abort();
	connectToNext();
}

/**
 *	@brief Disconnects the socket from julius
 *	
 *	@author Peter Grasch
 */
void RecognitionControl::disconnectFromServer()
{
	if (synchronisationOperation)
	{
		synchronisationOperation->canceled();
//		synchronisationOperation->deleteLater();
		synchronisationOperation=NULL;
	}
	if (modelCompilationOperation)
	{
		modelCompilationOperation->canceled();
//		modelCompilationOperation->deleteLater();
		modelCompilationOperation=NULL;
	}
	serverConnectionsToTry.clear();
	
	if (timeoutWatcher->isActive())
		timeoutWatcher->stop();
	
	if ((socket->state() == QAbstractSocket::UnconnectedState) ||
		(socket->state() == QAbstractSocket::ConnectingState) || 
		(socket->state() == QAbstractSocket::HostLookupState))
	{
		socket->abort();
		emit disconnected();
		return;
	}

	this->socket->disconnectFromHost();
	if ((socket->state() != QAbstractSocket::UnconnectedState) &&
		     (!socket->waitForDisconnected(500)))
		this->socket->abort();
}

/**
 *	@brief A Connection has been established
 *	
 *	Emits the connected() signal
 *	
 *	@author Peter Grasch
 */
void RecognitionControl::connectedTo()
{
	kDebug() << "Connected";
	timeoutWatcher->stop();

	emit connected();
	login();
}

/**
 * \brief Sends a simple request identified by the request id
 * \author Peter Grasch
 * @param request The request to send
 */
void RecognitionControl::sendRequest(qint32 request)
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << request;
	socket->write(toWrite);
}

void RecognitionControl::login()
{
	QString user = RecognitionConfiguration::juliusdUsername();
	QString pass = RecognitionConfiguration::juliusdPassword();

	if (user.isEmpty())
	{
		KPasswordDialog dlg( dynamic_cast<QWidget*>(parent()), 
					KPasswordDialog::ShowUsernameLine|KPasswordDialog::ShowKeepPassword );
		dlg.setPrompt( i18n( "Please enter your Authentication Details for the simond below" ));
		if( !dlg.exec() || dlg.username().isEmpty() )
		{
			disconnectFromServer(); //the user canceled
			return;
		}
		user = dlg.username();
		pass = dlg.password();

		if (dlg.keepPassword())
		{
			KSharedConfig::Ptr config = KSharedConfig::openConfig("simonrecognitionrc");
			KConfigGroup group(config, "Recognition");
			RecognitionConfiguration::setJuliusdUsername(user);
			RecognitionConfiguration::setJuliusdPassword(pass);
			group.writeEntry("JuliusdUsername", user);
			group.writeEntry("JuliusdPassword", KStringHandler::obscure(pass));
			config->sync();
		}
	}


	QByteArray userBytes = user.toUtf8();
	QByteArray passBytes = QCryptographicHash::hash(pass.toUtf8(),QCryptographicHash::Sha1);

	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	bodyStream << protocolVersion << userBytes << passBytes;
	
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::Login << (qint64) body.count();
	socket->write(toWrite);
	socket->write(body);
}

void RecognitionControl::sendActiveModelModifiedDate()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::ActiveModelDate
		<< ModelManagerUiProxy::getInstance()->getActiveContainerModifiedTime();
	socket->write(toWrite);
}
	
bool RecognitionControl::sendActiveModel()
{
	Model *model = ModelManagerUiProxy::getInstance()->createActiveContainer();
	if (!model) {
		emit synchronisationWarning(i18n("Couldn't create active model container"));
		sendRequest(Simond::ErrorRetrievingActiveModel);
		return false;
	}
	
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);

	bodyStream << ModelManagerUiProxy::getInstance()->getActiveContainerModifiedTime()
		<< model->sampleRate()
		<< model->hmmDefs()
		<< model->tiedList()
		<< model->data1()
		<< model->data2();

	out << (qint32) Simond::ActiveModel
		<< (qint64) body.count();

		
	socket->write(toWrite);
	socket->write(body);
	
	delete model;
	return true;
}


void RecognitionControl::sendActiveModelSampleRate()
{
	qint32 smpFreq = ModelManagerUiProxy::getInstance()->getActiveModelSampleRate();
	
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::ActiveModelSampleRate
		<< smpFreq;
		
	socket->write(toWrite);
}

void RecognitionControl::sendBaseModelDate()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::BaseModelDate
		<< ModelManagerUiProxy::getInstance()->getBaseModelDate();
	socket->write(toWrite);
}

bool RecognitionControl::sendBaseModel()
{
	kDebug() << "Sending base model";
	Model *model = ModelManagerUiProxy::getInstance()->createBaseModelContainer();
	if (!model) {
		emit synchronisationWarning(i18n("Couldn't create base model container"));
		sendRequest(Simond::ErrorRetrievingBaseModel);
		return false;
	}
	
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);

	bodyStream << ModelManagerUiProxy::getInstance()->getBaseModelDate()
		<< model->baseModelType()
		<< model->hmmDefs()
		<< model->tiedList()
		<< model->data1()
		<< model->data2();

	out << (qint32) Simond::BaseModel
		<< (qint64) body.count();
		
	socket->write(toWrite);
	socket->write(body);
	
	delete model;
	return true;

}

void RecognitionControl::requestMissingScenario()
{
	if (missingScenarios.isEmpty()) {
		sendRequest(Simond::ScenarioSynchronisationComplete);
		return;
	}

	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	QByteArray requestName = missingScenarios.takeAt(0).toUtf8();
	
	out << (qint32) Simond::GetScenario
		<< (qint64) (requestName.count()+sizeof(qint32)) /*sep*/ << requestName;
	socket->write(toWrite);
}

void RecognitionControl::sendScenarioList()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);

	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	
	bodyStream << ScenarioManager::getInstance()->getAllAvailableScenarioIds();
	
	out << (qint32) Simond::ScenarioList
		<< (qint64) body.count();
	socket->write(toWrite);
	socket->write(body);
}

void RecognitionControl::sendScenarioModifiedDate(QString scenarioId)
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);

	Scenario *s = new Scenario(scenarioId);
	if (!s->skim()) 
		out << (qint32) Simond::ErrorRetrievingScenario;
	else {
		kDebug() << "Sending scenario date of scenario " << scenarioId << ": " << s->modifiedDate();
		out << (qint32) Simond::ScenarioDate
			<< s->modifiedDate();
	}

	delete s;
	socket->write(toWrite);
}

void RecognitionControl::sendScenario(QString scenarioId)
{
	checkIfSynchronisationIsAborting();
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);

	kDebug() << "Sending scenario " << scenarioId;
	QFile f(KStandardDirs::locate("appdata", "scenarios/"+scenarioId));
	if (!f.open(QIODevice::ReadOnly)) {
		kDebug() << "Couldn't retreive scenario";
		out << (qint32) Simond::ErrorRetrievingScenario;
		socket->write(toWrite);
	} else {
		kDebug() << "Really sending scenario...";
		QByteArray body;
		QDataStream bodyStream(&body, QIODevice::WriteOnly);
		QByteArray scenarioIdByte = scenarioId.toUtf8();
		QByteArray scenarioByte = f.readAll();
		bodyStream << scenarioIdByte << scenarioByte;
		out << (qint32) Simond::Scenario << (qint64) (body.count());
		socket->write(toWrite);
		socket->write(body);
	}
}

void RecognitionControl::sendSelectedScenarioListModifiedDate()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);

	KSharedConfigPtr config = KSharedConfig::openConfig("simonscenariosrc");
	KConfigGroup cg(config, "");
	QDateTime lastModifiedDate = cg.readEntry("LastModified", QDateTime());

	out << (qint32) Simond::SelectedScenarioDate
			<< lastModifiedDate;

	socket->write(toWrite);
}

void RecognitionControl::sendSelectedScenarioList()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);

	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	
	KSharedConfigPtr config = KSharedConfig::openConfig("simonscenariosrc");
	KConfigGroup cg(config, "");
	QDateTime lastModifiedDate = cg.readEntry("LastModified", QDateTime());
	QStringList selectedScenarios = cg.readEntry("SelectedScenarios", QStringList());

	bodyStream << lastModifiedDate << selectedScenarios;
	
	out << (qint32) Simond::SelectedScenarioList
		<< (qint64) body.count();
	socket->write(toWrite);
	socket->write(body);

}


void RecognitionControl::sendLanguageDescriptionModifiedDate()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::LanguageDescriptionDate
		<< ModelManagerUiProxy::getInstance()->getLanguageDescriptionModifiedTime();
	kDebug() << "LANGUAGE DESCRIPTION MODIFIED TIME: " << ModelManagerUiProxy::getInstance()->getLanguageDescriptionModifiedTime();
	socket->write(toWrite);
}


void RecognitionControl::sendLanguageDescription()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	
	LanguageDescriptionContainer *languageDescription = ModelManagerUiProxy::getInstance()->getLanguageDescriptionContainer();
	
	bodyStream << ModelManagerUiProxy::getInstance()->getLanguageDescriptionModifiedTime()
		<< languageDescription->treeHed()
		<< languageDescription->shadowVocab();

	out << (qint32) Simond::LanguageDescription
		<< (qint64) body.count();
		
	socket->write(toWrite);
	socket->write(body);
	
	delete languageDescription;
}



void RecognitionControl::sendTrainingModifiedDate()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	out << (qint32) Simond::TrainingDate
		<< ModelManagerUiProxy::getInstance()->getTrainingModifiedTime();
	socket->write(toWrite);
}


void RecognitionControl::sendTraining()
{
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	QByteArray body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	
	TrainingContainer *training = ModelManagerUiProxy::getInstance()->getTrainingContainer();
	
	bodyStream << ModelManagerUiProxy::getInstance()->getTrainingModifiedTime()
		<< training->sampleRate()
		<< training->wavConfig()
		<< training->prompts();
	
	out << (qint32) Simond::Training
		<< (qint64) body.count();

	socket->write(toWrite);
	socket->write(body);
	
	delete training;
}

void RecognitionControl::synchronizeSamples()
{
	Q_ASSERT(ModelManagerUiProxy::getInstance());
	
	ModelManagerUiProxy::getInstance()->buildMissingSamplesList();
	fetchMissingSamples();
}

void RecognitionControl::fetchMissingSamples()
{
	Q_ASSERT(ModelManagerUiProxy::getInstance());
	
	QString sample = ModelManagerUiProxy::getInstance()->missingSample();
	if (sample.isNull())
	{
		kDebug() << "Done fetching samples";
		sendRequest(Simond::TrainingsSampleSynchronisationComplete);
		return;
	}
	
	QByteArray sampleByte = sample.toUtf8();
	
	QByteArray toWrite;
	QDataStream stream(&toWrite, QIODevice::WriteOnly);
	stream << (qint32) Simond::GetTrainingsSample
		<< (qint64) sampleByte.count()+sizeof(qint32) /*seperator*/
		<< sampleByte;
	socket->write(toWrite);
}


void RecognitionControl::sendSample(QString sampleName)
{
	checkIfSynchronisationIsAborting();
	QByteArray toWrite;
	QDataStream out(&toWrite, QIODevice::WriteOnly);
	
	sampleName += ".wav";
	
	QByteArray sample = ModelManagerUiProxy::getInstance()->getSample(sampleName);
	
	if (sample.isNull())
	{
		sendRequest(Simond::ErrorRetrievingTrainingsSample);
		if (synchronisationOperation)
			synchronisationOperation->canceled();

		synchronisationDone();
		sampleNotAvailable(sampleName);
		return;
	}

	out << (qint32) Simond::TrainingsSample
		<< (qint64) sample.count()+sizeof(qint32) /*separator*/
		<< sample;
	socket->write(toWrite);
}


void RecognitionControl::askStartSynchronisation()
{
	if (isConnected())
	{
		switch (RecognitionConfiguration::synchronizationMode())
		{
			case 0: //automatic
				startSynchronisation();
				break;
			case 1: //semi-automatic
				if (KMessageBox::questionYesNo(0, i18n("The Speech Model changed.\n\nSynchronize it now?"))==KMessageBox::Yes)
					startSynchronisation();
				break;
			case 2: //manual
				break;
		}
	}
}


void RecognitionControl::startSynchronisation()
{
	if (!isConnected()) return;

	if (synchronisationOperation)
	{
		synchronisationOperation->deleteLater();
	}

	synchronisationOperation = new Operation(thread(), i18n("Modell Synchronisation"), i18n("Initializing..."), 0, 100, false);

	kDebug() << "Starting synchronisation";
	ModelManagerUiProxy::getInstance()->startGroup();
	sendRequest(Simond::StartSynchronisation);
	kDebug() << stillToProcess.count();
}

void RecognitionControl::synchronisationComplete()
{//successful
	kDebug() << "Synchronisation completed";
	if (synchronisationOperation)
		synchronisationOperation->finished();
	synchronisationDone();
}

void RecognitionControl::synchronisationDone()
{
	kDebug() << "Finishing up synchronisation";

	if (synchronisationOperation)
	{
		if (!synchronisationOperation->isFinished())
		{
			synchronisationOperation->canceled();
		}
		synchronisationOperation=NULL;
	}

	ModelManagerUiProxy::getInstance()->commitGroup(true /*silent*/);
}


/**
 *	@brief Process the request
 *	
 *	Reads the new data from the socket and processes it
 *	
 *	@author Peter Grasch
 */
void RecognitionControl::messageReceived()
{
	qint32 type;
	Simond::Request request;
	QByteArray msgByte=stillToProcess;
	QDataStream msg(&msgByte, QIODevice::ReadOnly);
	bool messageNotYetFinished=false;
	while (socket->bytesAvailable())
	{
		messageLocker.lock();
		msgByte += socket->readAll();
		messageLocker.unlock();
		while (((unsigned) msg.device()->bytesAvailable() >= sizeof(qint32)) && !messageNotYetFinished)
		{
			messageNotYetFinished=false;
			msg >> type;
			request = (Simond::Request) type;
			switch (request)
			{
				////////////////////    LOGIN    //////////////////////////////////////
	
				case Simond::LoginSuccessful:
				{
					advanceStream(sizeof(qint32));
					emit loggedIn();
					switch (RecognitionConfiguration::synchronizationMode())
					{
						case 0: //automatic
							startSynchronisation();
							break;
						case 1: //semi-automatic
							if (KMessageBox::questionYesNo(0, i18n("Your speech model might have changed while you were disconnected.\n\n"
											"Do you want to start a synchronization now?"))==KMessageBox::Yes)
								startSynchronisation();
							break;
					}
					break;
				}
	
				case Simond::VersionIncompatible:
				{
					advanceStream(sizeof(qint32));
					emit simondSystemError(i18n("Version not supported"));
					break;
				}
	
				case Simond::AuthenticationFailed:
				{
					advanceStream(sizeof(qint32));
					emit simondSystemError(i18n("Wrong username or password."));
					this->disconnectFromServer();
					break;
				}
	
				case Simond::AccessDenied:
				{
					advanceStream(sizeof(qint32));
					emit simondSystemError(i18n("Access Denied."));
					this->disconnectFromServer();
					break;
				}
	
				////////////////////    SYNCHRONISATION    ////////////////////////////

				case Simond::SynchronisationAlreadyRunning:
				{
					advanceStream(sizeof(qint32));
					emit synchronisationError(i18n("The synchronization is already running.\n\nIf you are sure that this is a mistake, please disconnect from simond and re-connect."));
					synchronisationDone();
					break;
				}
	
				case Simond::AbortSynchronisationFailed:
				{
					advanceStream(sizeof(qint32));
					emit synchronisationError(i18n("Couldn't abort synchronization."));
					break;
				}

				case Simond::GetActiveModelDate:
				{
					if (!synchronisationOperation)
						synchronisationOperation = new Operation(thread(), i18n("Modell Synchronisation"), i18n("Synchronizing acitve Model"), 1, 100, false);
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					synchronisationOperation->update(i18n("Synchronizing acitve Model"), 1);
					kDebug() << "Server requested active Model modified date";
					sendActiveModelModifiedDate();
					break;
				}
	
				case Simond::GetActiveModel:
				{
					advanceStream(sizeof(qint32))
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server requested active Model";
					sendActiveModel();
					break;
				}
	
				case Simond::ActiveModel:
				{
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server sent active Model";
					
					parseLengthHeader();
					
					qint32 sampleRate;
					QByteArray hmmDefs, tiedList, data1, data2;
					
					QDateTime changedTime;
					msg >> changedTime;
					msg >> sampleRate;
					msg >> hmmDefs;
					msg >> tiedList;
					msg >> data1;
					msg >> data2;
					
					ModelManagerUiProxy::getInstance()->storeActiveModel(changedTime, sampleRate, 
									hmmDefs, tiedList, data1, data2);
					
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					sendBaseModelDate();
					break;
				}
				
				case Simond::GetActiveModelSampleRate:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					sendActiveModelSampleRate();
					break;
				}
	
				case Simond::NoActiveModelAvailable:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();

					kDebug() << "No active model available";
					emit synchronisationWarning(i18n("No Speech Model available: Recognition deactivated"));
					sendBaseModelDate();
					
					break;
				}
	
				case Simond::ActiveModelStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Couldn't store active model on server";
					emit synchronisationError(i18n("The server couldn't store the the active model."));
					sendBaseModelDate();
					
					break;
				}

				case Simond::GetBaseModelDate:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					sendBaseModelDate();
					break;
				}
				case Simond::GetBaseModel:
				{
					kDebug() << "Retreiving base model";
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					sendBaseModel();
					break;
				}
				case Simond::BaseModel:
				{
					kDebug() << "Server sent base model";

					parseLengthHeader();
					
					int baseModelType;
					QDateTime changedTime;
					QByteArray hmmDefs, tiedList, macros, stats;
					
					msg >> changedTime;
					msg >> baseModelType;
					msg >> hmmDefs;
					msg >> tiedList;
					msg >> macros;
					msg >> stats;
					
					ModelManagerUiProxy::getInstance()->storeBaseModel(changedTime, baseModelType, 
									hmmDefs, tiedList, macros, stats);
					
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);

					checkIfSynchronisationIsAborting();
					sendScenarioList();
					break;
				}
				case Simond::BaseModelStorageFailed:
				{
					kDebug() << "Base model storage failed";
					sendScenarioList();
					break;
				}
				
				case Simond::GetScenarioList:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					
					synchronisationOperation->update(i18n("Synchronizing scenarios"), 9);
	
					kDebug() << "Server requested scenario list";
					sendScenarioList();
					break;
				}
				
				case Simond::ScenarioList:
				{
					checkIfSynchronisationIsAborting();
					parseLengthHeader();

					QStringList remoteScenarioList;
					msg >> remoteScenarioList;
					missingScenarios.clear();
					QStringList localScenarioList = ScenarioManager::getInstance()->getAllAvailableScenarioIds();

					foreach (const QString& id, remoteScenarioList)
						if (!localScenarioList.contains(id))
							missingScenarios << id;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					synchronisationOperation->update(i18n("Synchronizing scenarios"), 9);
					kDebug() << "Server sent scenario list";
					sendRequest(Simond::StartScenarioSynchronisation);
					break;
				}

				case Simond::ScenarioStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server can't store scenario!";
					emit synchronisationError(i18n("The server couldn't store the scenario."));
					break;
				}
				
				case Simond::ScenarioSynchronisationComplete:
				{
					kDebug() << "Server sent scenario synchronizationcomplete";
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					requestMissingScenario();
					break;
				}
				
				case Simond::GetScenarioDate:
				{
					checkIfSynchronisationIsAborting();
					parseLengthHeader();

					QByteArray scenarioNameByte;
					msg >> scenarioNameByte;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server requested Scenario modified date for: " << scenarioNameByte;
					
					sendScenarioModifiedDate(QString::fromUtf8(scenarioNameByte));

					break;
				}
				
				case Simond::GetScenario:
				{
					checkIfSynchronisationIsAborting();
					parseLengthHeader();

					QByteArray scenarioNameByte;
					msg >> scenarioNameByte;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server requested Scenario: " << scenarioNameByte;
					
					sendScenario(QString::fromUtf8(scenarioNameByte));

					break;
				}
				
				case Simond::Scenario:
				{
					checkIfSynchronisationIsAborting();
					parseLengthHeader();

					QByteArray scenarioId;
					QByteArray scenario;
					msg >> scenarioId;
					msg >> scenario;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server sent Scenario " << scenarioId;
					
					if (!ScenarioManager::getInstance()->storeScenario(QString::fromUtf8(scenarioId), scenario))
						sendRequest(Simond::ScenarioStorageFailed);
					else  {
						sendRequest(Simond::ScenarioStored);
					}
					break;
				}
				
				case Simond::GetSelectedScenarioDate:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					kDebug() << "Server requested selected scenario date";
					sendSelectedScenarioListModifiedDate();
					break;
				}
				
				case Simond::GetSelectedScenarioList:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					kDebug() << "Server requested selected scenarios";
					sendSelectedScenarioList();
					break;
				}
				
				case Simond::SelectedScenarioList:
				{
					checkIfSynchronisationIsAborting();
					parseLengthHeader();

					QStringList list;
					msg >> list;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server sent Scenario list";

					KSharedConfigPtr config = KSharedConfig::openConfig("simonscenariosrc");
					KConfigGroup cg(config, "");
					cg.writeEntry("SelectedScenarios", list);
					cg.writeEntry("LastModified", QDateTime::currentDateTime());

					if (!ScenarioManager::getInstance()->setupScenarios())
						emit synchronisationError(i18n("Couldn't re-initialize scenarios. Please restart simon!"));
					break;
				}
				case Simond::SelectedScenarioListStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server could not store selected scenario list";
					emit synchronisationError(i18n("The server couldn't store the list of selected scenarios."));
					sendTrainingModifiedDate();
					break;
				}
				
				case Simond::GetTrainingDate:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					synchronisationOperation->update(i18n("Synchronizing Training"), 10);
	
					kDebug() << "Server Requested training modified date";
					sendTrainingModifiedDate();
					break;
				}
				
				case Simond::GetTraining:
				{
					synchronisationOperation->update(i18n("Sending Training-Corpus"), 11);
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server requested training";
					sendTraining();
					break;
				}
				
				case Simond::Training:
				{
					synchronisationOperation->update(i18n("Loading Training"), 11);
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server sent training";
					parseLengthHeader();
					qint32 sampleRate;
					QByteArray wavConfig, prompts;
					
					QDateTime changedTime;
					msg >> changedTime;
					msg >> sampleRate;
					msg >> wavConfig;
					msg >> prompts;
					
					ModelManagerUiProxy::getInstance()->storeTraining(changedTime, sampleRate,wavConfig,prompts);
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					synchronisationOperation->update(i18n("Synchronizing Wordlist"), 3);
					sendLanguageDescriptionModifiedDate();
					break;
				}
				
				case Simond::NoTrainingAvailable:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "No training available";
					sendLanguageDescriptionModifiedDate();
					break;
				}
				
				case Simond::TrainingStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
	
					kDebug() << "Server could not store training";
					emit synchronisationError(i18n("The server couldn't store the trainings corpus."));
					sendLanguageDescriptionModifiedDate();
					break;
				}
							
				case Simond::GetLanguageDescriptionDate:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();
					synchronisationOperation->update(i18n("Synchronizing Language Description"), 38);

					kDebug() << "Server Requested lang. desc. modified date";
					sendLanguageDescriptionModifiedDate();
					break;
				}
				
				case Simond::GetLanguageDescription:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();

					kDebug() << "Server requested lang. desc.";
					sendLanguageDescription();
					break;
				}
				
				case Simond::LanguageDescription:
				{
					checkIfSynchronisationIsAborting();

					parseLengthHeader();

					kDebug() << "Server sent languagedescription";
					
					QByteArray treeHed, shadowVocab;
					QDateTime changedTime;
					msg >> changedTime;
					msg >> treeHed;
					msg >> shadowVocab;
					ModelManagerUiProxy::getInstance()->storeLanguageDescription(changedTime,shadowVocab, treeHed);
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);

					sendRequest(Simond::StartTrainingsSampleSynchronisation);
					break;
				}
				
				case Simond::NoLanguageDescriptionAvailable:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();

					emit synchronisationError(i18n("There seems to be no language description available."));
					synchronisationDone();
					break;
				}
				
				case Simond::LanguageDescriptionStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();

					kDebug() << "Server could not store languagedescription";
					emit synchronisationError(i18n("The server couldn't store language description."));
					break;
				}

				case Simond::TrainingsSampleSynchronisationComplete:
				{
					advanceStream(sizeof(qint32));
					synchronizeSamples();
					break;
				}
				
				case Simond::ErrorRetrievingTrainingsSample:
				{
					advanceStream(sizeof(qint32));
					sampleNotAvailable(ModelManagerUiProxy::getInstance()->missingSample());
					synchronisationDone();
					break;
				}

				case Simond::GetTrainingsSample:
				{
					checkIfSynchronisationIsAborting();

					if (synchronisationOperation)
						synchronisationOperation->update(i18n("Synchronizing Trainings-Corpus"), 68);
					
					parseLengthHeader();
					
					QByteArray sampleNameByte;
					msg >> sampleNameByte;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server requested sampleNameByte";
					
					kDebug() << sampleNameByte;
					sendSample(QString::fromUtf8(sampleNameByte));
					break;
				}

				case Simond::TrainingsSample:
				{
					checkIfSynchronisationIsAborting();
					if (synchronisationOperation)
						synchronisationOperation->update(i18n("Synchronizing Trainings-Corpus"), 68);

					parseLengthHeader();
					
					QByteArray sample;
					msg >> sample;

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					kDebug() << "Server sent Trainings-Sample";
					
					if (!ModelManagerUiProxy::getInstance()->storeSample(sample))
					{
						sendRequest(Simond::TrainingsSampleStorageFailed);
						synchronisationDone();
					} else
						fetchMissingSamples();
					break;
				}
				
				
				case Simond::TrainingsSampleStorageFailed:
				{
					advanceStream(sizeof(qint32));
					checkIfSynchronisationIsAborting();

					kDebug() << "Server could not store trainings-sample";
					emit synchronisationError(i18n("The server couldn't store trainings sample."));
					synchronisationDone();
					break;
				}


				case Simond::SynchronisationCommitFailed:
				{
					advanceStream(sizeof(qint32));
					emit synchronisationError(i18n("Couldn't complete Synchronization"));
					synchronisationDone();
					break;
				}


				case Simond::SynchronisationComplete:
				{
					advanceStream(sizeof(qint32));
					synchronisationComplete();
					break;
				}


				case Simond::AvailableModels:
				{
					kDebug() << "Server sent Available Models";
					
					parseLengthHeader();
					
					QList<QDateTime> models;
					msg >> models;
					
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					emit modelsAvailable(models);
					break;
				}
				case Simond::SwitchToModelFailed:
				{
					advanceStream(sizeof(qint32));

					emit synchronisationError(i18n("Couldn't restore old model"));
					break;
				}
				
				
				////////////////////    COMPILATION    ////////////////////////////////



				case Simond::ModelCompilationStarted: {
					advanceStream(sizeof(qint32));
					if (modelCompilationOperation)
						modelCompilationOperation->canceled();
					modelCompilationOperation = new Operation(thread(), i18n("Compiling Model"), i18n("Initializing..."));
					break;
				}
				
				case Simond::ModelCompilationStatus: {
					
					parseLengthHeader();
					
					qint32 progNow, progMax;
					msg >> progNow;
					msg >> progMax;
					
					QString statusMsg;
					QByteArray statusByte;
					msg >> statusByte;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					statusMsg = QString::fromUtf8(statusByte);
					
					if (!modelCompilationOperation)
						modelCompilationOperation = new Operation(thread(), i18n("Compiling Model"), statusMsg, progNow, progMax);
					else 
						modelCompilationOperation->update(i18n("Model: %1", statusMsg), progNow, progMax);
					break;
				}
				
				case Simond::ModelCompilationError: {
					parseLengthHeader();

					QString errorMsg;
					QByteArray errorByte;
					QString protocol;
					QByteArray protocolByte;

					msg >> errorByte;
					msg >> protocolByte;
					
					errorMsg = QString::fromUtf8(errorByte);
					protocol = QString::fromUtf8(protocolByte);
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					if (modelCompilationOperation)
					{
						modelCompilationOperation->canceled();
						modelCompilationOperation=NULL;
					}
					emit compilationError(errorMsg, protocol);
					break;
				}

				case Simond::ModelCompilationAborted: {
					advanceStream(sizeof(qint32));
	
					if (modelCompilationOperation)
					{
						modelCompilationOperation->canceled();
						modelCompilationOperation=NULL;
					}
					break;
				}
	
				case Simond::ModelCompilationWordUndefined: {
					parseLengthHeader();
					QByteArray word;
					msg >> word;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					modelCompilationOperation->canceled();
					modelCompilationOperation=NULL;
					wordUndefined(QString::fromUtf8(word));
					break;
				}	
				case Simond::ModelCompilationClassUndefined: {
					parseLengthHeader();
					QByteArray undefClass;
					msg >> undefClass;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					modelCompilationOperation->canceled();
					modelCompilationOperation=NULL;
					classUndefined(QString::fromUtf8(undefClass));
					break;
				}	
				case Simond::ModelCompilationPhonemeUndefined: {
					parseLengthHeader();
					QByteArray phoneme;
					msg >> phoneme;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					modelCompilationOperation->canceled();
					modelCompilationOperation=NULL;
					phonemeUndefined(QString::fromUtf8(phoneme));
					break;
				}

				case Simond::ModelCompilationCompleted: {
					advanceStream(sizeof(qint32));
					modelCompilationOperation->finished();
					modelCompilationOperation=NULL;
					break;
				}

				//TODO: is this deprecated?
				case Simond::ErrorRetrievingModelCompilationProtocol: {
					advanceStream(sizeof(qint32));
					KMessageBox::sorry(0, i18n("Couldn't retrieve Model-Compilation-Protocol"));
					break;
				}
				
				case Simond::ModelCompilationProtocol: {
					parseLengthHeader();
					QByteArray protocol;
					msg >> protocol;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					displayCompilationProtocol(QString::fromUtf8(protocol));
				}
				

				////////////////////    RECOGNITION    ////////////////////////////////

				case Simond::RecognitionReady:
				{
					advanceStream(sizeof(qint32));

					emit recognitionStatusChanged(RecognitionControl::Ready);

					if (RecognitionConfiguration::automaticallyEnableRecognition())
						startRecognition();

					break;
				}

				case Simond::RecognitionAwaitingStream:
				{
					checkIfMessageFinished(sizeof(qint32));
					qint32 port, sampleRate;
					msg >> port;
					msg >> sampleRate;
					advanceStream(sizeof(qint32)*3);
					recognitionReady=true;

					kDebug() << "adinnet server running on port " << port;
					adinStreamer->init(socket->peerAddress(), port, sampleRate);
					startRecognition();
					break;
				}

				case Simond::RecognitionError:
				{
					parseLengthHeader();
					
					QByteArray errormsgByte;
					QByteArray protocolByte;
					msg >> errormsgByte;
					msg >> protocolByte;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);

					QString errormsg = QString::fromUtf8(errormsgByte);
					QString log = QString::fromUtf8(protocolByte);
					recognitionReady=false;
					emit recognitionError(errormsg, log);
					emit recognitionStatusChanged(RecognitionControl::Stopped);
					break;
				}

				case Simond::RecognitionWarning:
				{
					parseLengthHeader();
					
					QByteArray warningmsgByte;
					msg >> warningmsgByte;
					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					QString warningmsg = QString::fromUtf8(warningmsgByte);
					emit recognitionWarning(warningmsg);
					break;
				}

				case Simond::RecognitionStarted:
				{	
					advanceStream(sizeof(qint32));
						
					//nothing to do?
					break;
				}

				case Simond::RecognitionStopped:
				{
					recognitionReady=false;
					adinStreamer->stop();
					advanceStream(sizeof(qint32));
					emit recognitionStatusChanged(RecognitionControl::Stopped);
					break;
				}

				case Simond::RecognitionPaused:
				{
					advanceStream(sizeof(qint32));
					emit recognitionStatusChanged(RecognitionControl::Paused);
					break;
				}

				case Simond::RecognitionResumed:
				{
					advanceStream(sizeof(qint32));
					emit recognitionStatusChanged(RecognitionControl::Resumed);
					kDebug() << "Recognition has been resumed" << msgByte;
					break;
				}

				case Simond::RecognitionResult:
				{
					fprintf(stderr, "Simond::RecognitionResult\n");
					parseLengthHeader();

					qint8 sentenceCount;
					msg >> sentenceCount;

					RecognitionResultList* recognitionResults = new RecognitionResultList();

					for (int i=0; i < sentenceCount; i++) {
						QByteArray word, sampa, samparaw;
						QList<float> confidenceScores;
						msg >> word;
						msg >> sampa;
						msg >> samparaw;
						msg >> confidenceScores;
						recognitionResults->append(RecognitionResult(QString::fromUtf8(word), 
									QString::fromUtf8(sampa), 
									QString::fromUtf8(samparaw), 
									confidenceScores));
					}

					advanceStream(sizeof(qint32)+sizeof(qint64)+length);
					
					emit recognised(recognitionResults);
					break;
				}
				
				default:
					kDebug() << "Unknown request: " << request;
			}
		}

		messageNotYetFinished=false;

		//this is actually not the correct place (should be at the end of the function)
		//BUT: that way the very last thing the function does it check if there are still bytes available
		//this _ensures_ that we don't loose something
		stillToProcess=msgByte;
	}
}


bool RecognitionControl::getAvailableModels()
{
	sendRequest(Simond::GetAvailableModels);
	return true;
}

bool RecognitionControl::switchToModel(const QDateTime& model)
{
	QByteArray toWrite, body;
	QDataStream bodyStream(&body, QIODevice::WriteOnly);
	QDataStream stream(&toWrite, QIODevice::WriteOnly);
	
	bodyStream << model;
	stream << (qint32) Simond::SwitchToModel << (qint64) body.count();

	socket->write(toWrite);
	socket->write(body);
	return true;
}

void RecognitionControl::fetchCompilationProtocol()
{
	sendRequest(Simond::GetModelCompilationProtocol);
}

void RecognitionControl::startRecognition()
{
	if (recognitionReady)
	{
		kDebug() << "ole bin hier";
		adinStreamer->start();
	} else
		sendRequest(Simond::StartRecognition);
}


void RecognitionControl::stopRecognition()
{
	adinStreamer->stop();
	sendRequest(Simond::StopRecognition);
}

void RecognitionControl::pauseRecognition()
{
//	kDebug() << "Sending pause request";
	adinStreamer->stop();
//	sendRequest(Simond::PauseRecognition);
}

void RecognitionControl::resumeRecognition()
{
	kDebug() << "Sending resume request";
	adinStreamer->start();
//	sendRequest(Simond::ResumeRecognition);
}

void RecognitionControl::displayCompilationProtocol(const QString& protocol)
{
	KMessageBox::detailedSorry(0, i18n("Protocol:"), protocol);
}

void RecognitionControl::sampleNotAvailable(const QString& sample)
{
	if (KMessageBox::questionYesNo(0, i18n("The sample \"%1\" could not be found neither on the local computer nor the "
"server.\n\nDo you want to remove it from the Trainings-Database?", sample)) == KMessageBox::Yes)
	{
		//kick some poor samples ass
		ModelManagerUiProxy::getInstance()->startGroup();
		QString sampleBaseName = sample.left(sample.length()-4);
		kDebug() << "Deleting: " << sampleBaseName;
		bool succ = TrainingManager::getInstance()->removeSample(sampleBaseName);
		if (succ)
			TrainingManager::getInstance()->savePrompts();

		ModelManagerUiProxy::getInstance()->commitGroup(false /*silent*/);

		if (!succ)
			KMessageBox::error(0, i18n("Couldn't remove Sample from the Trainingscorpus"));
	}
}




void RecognitionControl::wordUndefined(const QString& word)
{
	int ret = KMessageBox::questionYesNoCancel(0, 
			i18n("The word \"%1\" is used in your training-samples but is not contained "
			"in your wordlist.\n\nWhat do you want to do?", word), QString(), 
			KGuiItem(i18n("Remove samples containing the word"), KIcon("list-remove")), 
			KGuiItem(i18n("Add the word"), KIcon("list-add")));

	switch (ret)
	{
		case KMessageBox::Cancel:
			return;
		case KMessageBox::Yes:
			//removing the samples
			if (!TrainingManager::getInstance()->deleteWord(word))
				KMessageBox::information(0,
					i18n("Failed to remove word \"%1\" from the trainingscorpus"));
			break;
		case KMessageBox::No:
			//adding the word
			AddWordView *addWordView = new AddWordView(0);
			addWordView->createWord(word);
			addWordView->show();
			connect(addWordView, SIGNAL(finished(int)), addWordView, SLOT(deleteLater()));
			break;
	}
}

void RecognitionControl::classUndefined(const QString& undefClass)
{
	KMessageBox::sorry(0, i18n("Your grammar uses the undefined terminal \"%1\".\n\nPlease add a word that uses this terminal or remove the structure(s) containing the terminal from your grammar.", undefClass));
}

void RecognitionControl::phonemeUndefined(const QString& phoneme)
{
	KMessageBox::sorry(0, i18n("The Phoneme \"%1\" is undefined.\n\nPlease train at least one word that uses it.", phoneme));
}

/**
 *	@brief Destructor
 *	
 *	@author Peter Grasch
 */
RecognitionControl::~RecognitionControl()
{
    delete socket;
    delete timeoutWatcher;
}


