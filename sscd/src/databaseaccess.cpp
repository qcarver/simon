/*
 *   Copyright (C) 2008 Peter Grasch <peter.grasch@bedahr.org>
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

#include "databaseaccess.h"
#include "sscqueries.h"
#include "mysqlqueries.h"
#include <sscobjects/user.h>
#include <sscobjects/microphone.h>
#include <sscobjects/soundcard.h>
#include <sscobjects/language.h>
#include <sscobjects/sample.h>
#include <sscobjects/institution.h>
#include <sscobjects/userininstitution.h>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDriver>
#include <QFile>
#include <QDebug>
#include <QVariant>
#include <QSqlDatabase>
#include <QStringList>
#include <QMutexLocker>

#ifdef MYSQL_PING_WORKAROUND
#include <mysql/mysql.h>
#endif

/*
 * Constructor
 */
DatabaseAccess::DatabaseAccess(QObject* parent) : QObject(parent),
db(0), queryProvider(0), transactionLock(QMutex::Recursive)
{
}


/*
 * Connects to the database and sets up the query provider;
 * \param type specifies the database software in use (may be SQLITE to use a built-in one; In this case set dbName to filename
 * \param dbName name of the database to use
 * \param user database user
 * \param password database user password
 * \param options connection options
 */
bool DatabaseAccess::init(const QString& type, const QString& host, qint16 port, const QString& dbName, const QString& user, const QString& password, const QString& options)
{
  if(db) {
    db->close();
    delete db;
    QSqlDatabase::removeDatabase("defaultConnection");
    delete queryProvider;
  }

  db = new QSqlDatabase(QSqlDatabase::addDatabase(type));

  db->setHostName(host);
  db->setPort(port);
  db->setUserName(user);
  db->setPassword(password);
  db->setDatabaseName(dbName);
  db->setConnectOptions(options);

  emit error("Host: "+host.toUtf8()+'\n'+
    "Port: "+QString::number(port).toUtf8()+'\n'+
    "User: "+user.toUtf8()+'\n'+
    "Password: "+password.toUtf8()+'\n'+
    "Database Name: "+dbName.toUtf8());

  if (type == "QMYSQL") queryProvider = new MYSQLQueries();
  //else queryProvider = new SSCQueries();

  if (!db->open()) {                              //open database
    emit error(db->lastError().text());
    return false;
  }
  qDebug() << "Database opened...";

  return true;
}


/**
 * Locks the transaction mutex; Use this to execute multiple queries (even across multiple function calls)
 * that absolutely need to be sequential (prime example: LAST_INSERT_ID())
 */
void DatabaseAccess::lockTranscation()
{
  transactionLock.lock();
}


/**
 * Unlocks the transaction mutex; Use this to execute multiple queries (even across multiple function calls)
 * that absolutely need to be sequential (prime example: LAST_INSERT_ID())
 *
 * You MUST NOT FORGET to call this after you called lockTransaction or ALL client threads will HANG on their
 * next database request (forever).
 */
void DatabaseAccess::unlockTransaction()
{
  transactionLock.unlock();
}


/*
 * Closes the database connection but doesn't delete the handle
 */
void DatabaseAccess::closeConnection()
{
  db->close();
}


/*
 * Returns true if the database handle exists (init() was called) and
 * is connected to a database
 * \see init()
 */
bool DatabaseAccess::isConnected()
{
  return db && db->isOpen();
}


/**
 * Executes the query and returns the success;
 * If the query fails, the error message is propagated through error()
 */
bool DatabaseAccess::executeQuery(QSqlQuery& query)
{
  QVariant v = db->driver()->handle();

  #ifdef MYSQL_PING_WORKAROUND
  if (v.typeName() == QLatin1String("MYSQL*")) {
    MYSQL *handle = *static_cast<MYSQL **>(v.data());
    if (handle != 0) {
      qDebug() << "Old thread id: " << mysql_thread_id(handle);
      qDebug() << "mysql_ping() result: " << mysql_ping(handle);
      qDebug() << "New thread id: " << mysql_thread_id(handle);
    }
  }
  #endif

  bool succ = query.exec(); // krazy:exclude=crashy
  if (!succ)
    emit error(query.lastError().text());
  return succ;
}


/**
 * Asks the database for the last inserted auto-increment id
 */
int DatabaseAccess::getLastInsertedId()
{
  QSqlQuery q = queryProvider->lastInsertedId();
  if (!executeQuery(q) || !q.next()) return 0;

  return q.value(0).toInt();
}


/**
 * Returns the requested user or 0 if the user was not found
 */
User* DatabaseAccess::getUser(qint32 id)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->getUser();

  q.bindValue(":userid", id);

  if (!executeQuery(q) || !q.first()) return 0;

  return new User(q.value(0).toInt(),
    q.value(1).toString(),
    q.value(2).toString(),
    q.value(3).toString().at(0),
    q.value(4).toInt(),
    q.value(5).toString(),
    q.value(6).toString(),
    q.value(7).toString(),
    q.value(8).toString(),
    q.value(9).toString(),
    q.value(10).toString(),
    q.value(11).toInt(),
    q.value(12).toInt(),
    q.value(13).toInt(),
    q.value(14).toString(),
    q.value(15).toBool(),
    q.value(16).toBool());
}


/**
 * Returns all users matching the given filter criteria
 * Criteria that doesn't contain valid data will be ignored
 */
QList<User*> DatabaseAccess::getUsers(User* u, qint32 institutionId, const QString& referenceId, bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  bool includeUserId = (u->userId() > 0);
  bool includeSurname = (!u->surname().isEmpty());
  bool includeGivenName = (!u->givenName().isEmpty());
  bool includeSex = ((u->sex() == 'M') || (u->sex() == 'F'));
  bool includeBirthYear = (u->birthYear() > 1900);
  bool includeZipcode = (!u->zipCode().isEmpty());
  bool includeEducation = (!u->education().isEmpty());
  bool includeCurrentOccupation = (!u->currentOccupation().isEmpty());
  bool includeMotherTongue = (!u->motherTongueId().isEmpty());
  bool includeDiagnosis = (!u->diagnosis().isEmpty());
  bool includeOrientation = (u->orientation() > 0);
  bool includeMotorfunction = (u->motorFunction() > 0);
  bool includeCommunication = (u->communication() > 0);
  bool includeMouthMotoric = (!u->mouthMotoric().isEmpty());
  bool includeInterviewPossible = (u->interviewPossible() < 2);
  bool includeRepeatingPossible = (u->repeatingPossible() < 2);
  bool includeInstitutionId = (institutionId > 0);
  bool includeReferenceId = ((institutionId > 0) && !referenceId.isEmpty());

  QSqlQuery q = queryProvider->getUsers(includeUserId, includeSurname,
    includeGivenName, includeSex, includeBirthYear,
    includeZipcode, includeEducation, includeCurrentOccupation,
    includeMotherTongue, includeDiagnosis, includeOrientation,
    includeMotorfunction, includeCommunication, includeMouthMotoric,
    includeInterviewPossible, includeRepeatingPossible, includeInstitutionId,
    includeReferenceId);

  if (includeUserId) q.bindValue(":userid", u->userId());
  if (includeSurname) q.bindValue(":surname", '%'+u->surname()+'%');
  if (includeGivenName) q.bindValue(":forename", '%'+u->givenName()+'%');
  if (includeSex) q.bindValue(":sex", u->sex());
  if (includeBirthYear) q.bindValue(":birthyear", u->birthYear());
  if (includeZipcode) q.bindValue(":zipcode", '%'+u->zipCode()+'%');
  if (includeEducation) q.bindValue(":education", '%'+u->education()+'%');
  if (includeCurrentOccupation) q.bindValue(":currentoccupation", '%'+u->currentOccupation()+'%');
  if (includeMotherTongue) q.bindValue(":mothertongue", u->motherTongueId());
  if (includeDiagnosis) q.bindValue(":diagnosis", '%'+u->diagnosis()+'%');
  if (includeOrientation) q.bindValue(":orientation", u->orientation());
  if (includeMotorfunction) q.bindValue(":motorfunction", u->motorFunction());
  if (includeCommunication) q.bindValue(":communication", u->communication());
  if (includeMouthMotoric) q.bindValue(":mouthmotoric", '%'+u->mouthMotoric()+'%');
  if (includeInterviewPossible) q.bindValue(":interviewpossible", u->interviewPossible());
  if (includeRepeatingPossible) q.bindValue(":repeatpossible", u->repeatingPossible());
  if (includeInstitutionId) q.bindValue(":institutionId", institutionId);
  if (includeReferenceId) q.bindValue(":referenceId", referenceId);

  if (!executeQuery(q)) {
    return QList<User*>();
  }

  QList<User*> users;
  while (q.next()) {
    users.append(new User(q.value(0).toInt(),
      q.value(1).toString(),
      q.value(2).toString(),
      q.value(3).toString().at(0),
      q.value(4).toInt(),
      q.value(5).toString(),
      q.value(6).toString(),
      q.value(7).toString(),
      q.value(8).toString(),
      q.value(9).toString(),
      q.value(10).toString(),
      q.value(11).toInt(),
      q.value(12).toInt(),
      q.value(13).toInt(),
      q.value(14).toString(),
      q.value(15).toBool(),
      q.value(16).toBool()));
  }
  if (success != 0)
    *success = true;
  return users;
}


bool DatabaseAccess::addUser(User *u, int& userId)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->addUser();

  q.bindValue(":surname", u->surname());
  q.bindValue(":forename", u->givenName());
  q.bindValue(":sex", u->sex());
  q.bindValue(":birthyear", u->birthYear());
  q.bindValue(":zipcode", u->zipCode());
  q.bindValue(":education", u->education());
  q.bindValue(":currentoccupation", u->currentOccupation());
  q.bindValue(":mothertongue", u->motherTongueId());
  q.bindValue(":diagnosis", u->diagnosis());
  q.bindValue(":orientation", u->orientation());
  q.bindValue(":motorfunction", u->motorFunction());
  q.bindValue(":communication", u->communication());
  q.bindValue(":mouthmotoric", u->mouthMotoric());
  q.bindValue(":interviewpossible", u->interviewPossible());
  q.bindValue(":repeatpossible", u->repeatingPossible());

  if (!executeQuery(q)) {
    return false;
  }

  userId = getLastInsertedId();
  return true;
}


/**
 * Adds a new user - institution association; If it fails, this function will return
 * an error code:
 * 	0: success
 * 	-1: wrong user id
 * 	-2: wrong institution id
 */
int DatabaseAccess::addUserInstitutionAssociation(UserInInstitution *uii)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->addUserInstitutionAssociation();

  q.bindValue(":userid", uii->userId());
  q.bindValue(":institutionid", uii->institutionId());
  q.bindValue(":referenceid", uii->referenceId());

  if (!executeQuery(q)) {
    //determine cause of failure
    Institution *i = getInstitution(uii->institutionId());
    if (i) {
      delete i;
      return -1;
    }
    return -2;
  }
  return 0;
}


/**
 * Deletes a user - institution association
 */
bool DatabaseAccess::deleteUserInstitutionAssociation(qint32 userId, qint32 institutionId)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->deleteUserInstitutionAssociation();

  q.bindValue(":userid", userId);
  q.bindValue(":institutionid", institutionId);

  return executeQuery(q);
}


QList<UserInInstitution*> DatabaseAccess::getUserInstitutionAssociation(qint32 userId, bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  QList<UserInInstitution*> uiis;

  QSqlQuery q = queryProvider->getUserInstitutionAssociation();
  q.bindValue(":userid", userId);

  qDebug() << "Executing query...";
  if (!executeQuery(q)) return QList<UserInInstitution*>();
  qDebug() << "Survived query...";

  while (q.next()) {
    uiis.append(new UserInInstitution(q.value(0).toInt(),
      q.value(1).toInt(), q.value(2).toString()));
  }

  return uiis;
}


bool DatabaseAccess::modifyUser(User *u)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->modifyUser();

  q.bindValue(":userid", u->userId());
  q.bindValue(":surname", u->surname());
  q.bindValue(":forename", u->givenName());
  q.bindValue(":sex", u->sex());
  q.bindValue(":birthyear", u->birthYear());
  q.bindValue(":zipcode", u->zipCode());
  q.bindValue(":education", u->education());
  q.bindValue(":currentoccupation", u->currentOccupation());
  q.bindValue(":mothertongue", u->motherTongueId());
  q.bindValue(":diagnosis", u->diagnosis());
  q.bindValue(":orientation", u->orientation());
  q.bindValue(":motorfunction", u->motorFunction());
  q.bindValue(":communication", u->communication());
  q.bindValue(":mouthmotoric", u->mouthMotoric());
  q.bindValue(":interviewpossible", u->interviewPossible());
  q.bindValue(":repeatpossible", u->repeatingPossible());

  return executeQuery(q);
}


bool DatabaseAccess::removeUser(qint32 id)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->removeUser();

  q.bindValue(":userid", id);

  return executeQuery(q);
}


QList<Language*> DatabaseAccess::getLanguages(bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  QList<Language*> ll;

  QSqlQuery q = queryProvider->getLanguages();

  if (!executeQuery(q))
    return QList<Language*>();

  while (q.next()) {
    ll.append(new Language(q.value(0).toString(),
      q.value(1).toString()));
  }
  if (success != 0)
    *success = true;
  return ll;
}


QList<Microphone*> DatabaseAccess::getMicrophones(bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  QList<Microphone*> ml;

  QSqlQuery q = queryProvider->getMicrophones();

  if (!executeQuery(q))
    return QList<Microphone*>();

  while (q.next()) {
    ml.append(new Microphone(q.value(0).toInt(),
      q.value(1).toString(), q.value(2).toString()));
  }
  if (success != 0)
    *success = true;
  return ml;
}


QList<SoundCard*> DatabaseAccess::getSoundCards(bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  QList<SoundCard*> sl;

  QSqlQuery q = queryProvider->getSoundCards();

  if (!executeQuery(q))
    return QList<SoundCard*>();

  while (q.next()) {
    sl.append(new SoundCard(q.value(0).toInt(),
      q.value(1).toString(), q.value(2).toString()));
  }

  if (success != 0)
    *success = true;
  return sl;
}


bool DatabaseAccess::getOrCreateMicrophone(Microphone *m, qint32& microphoneId)
{
  QMutexLocker l(&transactionLock);

  microphoneId = -1;

  QSqlQuery q = queryProvider->getMicrophone();
  q.bindValue(":model", m->model());
  q.bindValue(":type", m->type());

  if (!executeQuery(q) || !q.first()) {
    // need to create
    q = queryProvider->createMicrophone();
    q.bindValue(":model", m->model());
    q.bindValue(":type", m->type());

    if (!executeQuery(q)) return false;

    microphoneId = getLastInsertedId();
  }
  else {
    microphoneId = q.value(0).toInt();
  }

  return true;
}


bool DatabaseAccess::getOrCreateSoundCard(SoundCard *s, qint32& soundCardId)
{
  QMutexLocker l(&transactionLock);

  soundCardId = -1;

  QSqlQuery q = queryProvider->getSoundCard();
  q.bindValue(":model", s->model());
  q.bindValue(":type", s->type());

  if (!executeQuery(q) || !q.first()) {
    // need to create
    q = queryProvider->createSoundCard();
    q.bindValue(":model", s->model());
    q.bindValue(":type", s->type());

    if (!executeQuery(q)) return false;

    soundCardId = getLastInsertedId();
  }
  else {
    soundCardId = q.value(0).toInt();
  }

  return true;
}


Institution* DatabaseAccess::getInstitution(qint32 id)
{
  QMutexLocker l(&transactionLock);

  QSqlQuery q = queryProvider->getInstitutions();
  q.bindValue(":institutionid", id);

  if (!executeQuery(q) || !q.first()) return 0;

  return new Institution(q.value(0).toInt(), q.value(1).toString());
}


QList<Institution*> DatabaseAccess::getInstitutions(bool *success)
{
  if (success != 0)
    *success = false;
  QMutexLocker l(&transactionLock);
  QList<Institution*> ins;

  QSqlQuery q = queryProvider->getInstitutions();

  if (!executeQuery(q)) return QList<Institution*>();

  while (q.next()) {
    ins.append(new Institution(q.value(0).toInt(),
      q.value(1).toString()));
  }
  if (success != 0)
    *success = true;
  return ins;
}


bool DatabaseAccess::addInstitution(Institution *i)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->addInstitution();

  q.bindValue(":name", i->name());

  return executeQuery(q);
}


bool DatabaseAccess::modifyInstitution(Institution *i)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->modifyInstitution();

  q.bindValue(":institutionid", i->id());
  q.bindValue(":name", i->name());

  return executeQuery(q);
}


bool DatabaseAccess::removeInstitution(qint32 id)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->removeInstitution();

  q.bindValue(":institutionid", id);

  return executeQuery(q);
}


qint32 DatabaseAccess::nextSampleId()
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->lastSampleId();
  if (!executeQuery(q)) return -1;

  if (!q.first())                                 //no rows
    return 1;

  return q.value(0).toInt()+1;
}


bool DatabaseAccess::storeSample(Sample *s)
{
  QMutexLocker l(&transactionLock);
  QSqlQuery q = queryProvider->addSample();

  q.bindValue(":sampleid", s->sampleId());
  q.bindValue(":userid", s->userId());
  q.bindValue(":microphoneid", s->microphoneId());
  q.bindValue(":soundcardid", s->soundCardId());
  q.bindValue(":typeid", s->typeId());
  q.bindValue(":prompt", s->prompt());
  q.bindValue(":path", s->path());

  return executeQuery(q);
}


QStringList* DatabaseAccess::getSamplePaths(qint32 id)
{
  QMutexLocker l(&transactionLock);
  QStringList* paths = new QStringList();

  QSqlQuery q = queryProvider->getSamplePaths();
  q.bindValue(":userid", id);

  if (!executeQuery(q)) return 0;

  while (q.next())
    paths->append(q.value(0).toString());

  return paths;
}


DatabaseAccess::~DatabaseAccess()
{
  if (db) {
    db->close();
    delete db;
    delete queryProvider;
  }
}
