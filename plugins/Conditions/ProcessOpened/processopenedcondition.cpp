#include "processopenedcondition.h"
#include <KDebug>

K_PLUGIN_FACTORY( ProcessOpenedPluginFactory,
registerPlugin< ProcessOpenedCondition >();
)

K_EXPORT_PLUGIN( ProcessOpenedPluginFactory("simonprocessopenedcondition") )

ProcessOpenedCondition::ProcessOpenedCondition(QObject *parent, const QVariantList &args) :
    Condition(parent, args)
{
}

void ProcessOpenedCondition::privateDeSerialize(QDomElement elem)
{
    //get the process name
    m_processName = elem.firstChildElement("Opened").firstChildElement("Name").text();

    //connect to the ProcessInfo instance
    ProcessInfo* processInfo = ProcessInfo::instance();

    connect(processInfo, SIGNAL(processAdded(QString)),
            this, SLOT(checkAddedProcess(QString)));
    connect(processInfo, SIGNAL(processRemoved(QString)),
            this, SLOT(checkRemovedProcess(QString)));

    //initialize the condition
    if (processInfo->getRunningProcesses().contains(m_processName))
    {
        m_satisfied = true;
	kDebug() << m_processName + " is opened!";
        emit conditionChanged();
	
    }
    else
    {
        m_satisfied = false;
	kDebug() << m_processName + " is not opened!";
        emit conditionChanged();
    }
}

void ProcessOpenedCondition::checkAddedProcess(QString name)
{
    if (m_processName == name)
    {
        m_satisfied = true;
	kDebug() << m_processName + " is opened!";
        emit conditionChanged();
    }
}

void ProcessOpenedCondition::checkRemovedProcess(QString name)
{
    if (m_processName == name)
    {
        m_satisfied = false;
	kDebug() << m_processName + " is not opened!";
        emit conditionChanged();
    }
}
