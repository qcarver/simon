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

#ifndef SIMON_RECOGNITIONCONTROL_H_F4CB407566D6459487054C2B03311E38
#define SIMON_RECOGNITIONCONTROL_H_F4CB407566D6459487054C2B03311E38

#include <QObject>
#include <QThread>
#include <QList>
#include <QDateTime>
#include <QMetaType>
#include <simonrecognitionresult/recognitionresult.h>

class RecognitionControl : public QThread
{
  Q_OBJECT

  private:
    int m_refCounter;
    
  protected:
    QString m_lastModel;

  signals:
    void recognitionReady();
    void recognitionError(const QString& error, const QByteArray& buildLog);
    void recognitionWarning(const QString& warning);
    void recognitionStarted();
    void recognitionStopped();
    void recognitionPaused();
    void recognitionResumed();
    void recognitionResult(const QString& fileName, const RecognitionResultList& recognitionResults);
    void recognitionDone(const QString& fileName);

  protected:
    QString username;
    int m_startRequests;

  public:
    explicit RecognitionControl(const QString& username, QObject *parent=0);

    virtual bool initializeRecognition(const QString& modelPath)=0;
    virtual bool isInitialized()=0;
    
    virtual bool startRecognition()=0;
    
    virtual bool stop()=0;
    virtual bool suspend()=0; //stop temporarily (still reflect the intention of being active, but don't do any recognition)

    virtual void recognize(const QString& fileName)=0;
    
    bool recognitionRunning();
    
    void pop();
    void push();
    bool isEmpty() const;

    ~RecognitionControl();

};
#endif
