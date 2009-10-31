/*
 *   Copyright (C) 2009 Peter Grasch <grasch@simon-listens.org>
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

#include "actioncollection.h"
#include <KDebug>
#include <simonscenarios/action.h>
#include <simonscenarios/scenario.h>
#include <simonscenarios/createcommandwidget.h>
#include <simonscenarios/commandmanager.h>
#include <simonscenarios/actioncommandmodel.h>
#include "actioncommandmodel.h"

ActionCollection::ActionCollection(Scenario *parent) : ScenarioObject(parent)
{
	proxy = new ActionCommandModel(this);

}

/**
 * Factory function
 * \author Peter Grasch
 */
ActionCollection* ActionCollection::createActionCollection(Scenario *parent, const QDomElement& actionCollectionElem)
{
	ActionCollection *ac = new ActionCollection(parent);
	if (!ac->deSerialize(actionCollectionElem)) {
		delete ac;
		ac=NULL;
	} 
	return ac;
}

bool ActionCollection::deSerialize(const QDomElement& actionCollectionElem)
{
	if (actionCollectionElem.isNull())
		return false;
	
	//clean member
	qDeleteAll(m_actions);
	m_actions.clear();

	QDomElement pluginElem = actionCollectionElem.firstChildElement();
	while (!pluginElem.isNull()) {
		Action *a = Action::createAction(parentScenario, pluginElem);
		if (!a) {
			kDebug() << "Couldn't load action";
		} else {
			m_actions << a;
		}

		pluginElem = pluginElem.nextSiblingElement();
	}
	proxy->update();
	reset();
	return true;
}

QDomElement ActionCollection::serialize(QDomDocument *doc)
{
	QDomElement actionsElem = doc->createElement("actions");
	foreach (Action *a, m_actions) {
		actionsElem.appendChild(a->serialize(doc));
	}
	proxy->update();
	return actionsElem;
}


QList<CreateCommandWidget*>* ActionCollection::getCreateCommandWidgets(QWidget *parent)
{
	QList<CreateCommandWidget*> *out = new QList<CreateCommandWidget*>();
	
	foreach (Action* action, m_actions)
	{
		CreateCommandWidget* widget = (CreateCommandWidget*) action->manager()->getCreateCommandWidget(parent);
		if (widget)
			*out << widget;
	}
	return out;
}

QList<KCModule*>* ActionCollection::getConfigurationWidgets(QWidget *parent)
{

}

/*bool ActionCollection::addCommand(Command *command)
{
	if (!command) return false;
	
	int i=0;
	bool added=false;
	while (!added && (i< m_actions.count())) {
		CommandManager *man = m_actions.at(i)->manager();
		added = man->addCommand(command);
		i++;
	}

	proxy->update();
	return added;
}*/

bool ActionCollection::removeCommand(Command *command)
{
	bool removed=false;
	foreach (Action *a, m_actions) {
		if (a->removeCommand(command)) {
			removed = true;
			break;
		}
	}
	proxy->update();
	return removed;
}

bool ActionCollection::addAction(Action *action)
{
	action->assignParent(parentScenario);
	action->deSerialize(QDomElement());

	beginInsertRows(QModelIndex(), rowCount(), rowCount());
	m_actions << action;
	endInsertRows();

	proxy->update();

	return parentScenario->save();
}

bool ActionCollection::deleteAction(Action *action)
{
	for (int i=0; i <m_actions.count(); i++) {
		if (m_actions[i] == action) {
			beginRemoveRows(QModelIndex(), i, i);
			m_actions.takeAt(i);
			endRemoveRows();
		}
	}
	delete action;

	proxy->update();
	return parentScenario->save();
}

bool ActionCollection::moveActionUp(Action *action)
{
	bool moved = false;

	for (int i=1; i <m_actions.count(); i++) {
		if (m_actions[i] == action) {
			m_actions.takeAt(i);
			m_actions.insert(i-1, action);
			emit dataChanged(index(i-1, 0), 
					  index(i, columnCount()));
			moved = true;
			break;
		}
	}
	proxy->update();

	return moved;
}

bool ActionCollection::moveActionDown(Action *action)
{
	bool moved = false;

	for (int i=0; i <m_actions.count()-1; i++) {
		if (m_actions[i] == action) {
			m_actions.takeAt(i);
			m_actions.insert(i+1, action);
			emit dataChanged(index(i, 0), 
					  index(i+1, columnCount()));
			moved = true;
			break;
		}
	}
	proxy->update();

	return moved;
}


ActionCollection::~ActionCollection()
{
	qDeleteAll(m_actions);
}

