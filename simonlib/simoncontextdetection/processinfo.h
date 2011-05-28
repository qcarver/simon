#ifndef PROCESSINFO_H
#define PROCESSINFO_H

#include <QThread>
#include "processinfogatherer.h"
#include "simoncontextdetection_export.h"

class SIMONCONTEXTDETECTION_EXPORT ProcessInfo : public QObject
{
    Q_OBJECT
public:
    static ProcessInfo* instance();

    QStringList getRunningProcesses() {return m_runningProcesses;}

private:
    explicit ProcessInfo();
    //ProcessInfo(const ProcessInfo&) {}
    //ProcessInfo& operator=(const ProcessInfo&) {}

    QStringList m_runningProcesses;
    ProcessInfoGatherer *m_gatherer;
    static ProcessInfo *m_instance;

signals:
    void processAdded(QString);
    void processRemoved(QString);

public slots:
    void checkRunningProcesses(QStringList names) {m_runningProcesses = names;}
    void checkAddedProcesses(QString name) {emit processAdded(name);}
    void checkRemovedProcesses(QString name) {emit processRemoved(name);}
};

#endif // PROCESSINFO_H
