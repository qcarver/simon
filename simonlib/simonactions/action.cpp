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

#include "action.h"
#include <KService>
#include <KPluginInfo>
#include <commandpluginbase/commandmanager.h>

Action::Action(const QString& source, const QString& trigger) : m_source(source)
{
	KService::Ptr service = KService::serviceByStorageId(source);
	KPluginInfo pluginInfo(service);
	m_enabledByDefault = pluginInfo.isPluginEnabledByDefault();
	KPluginFactory *factory = KPluginLoader(service->library()).factory();
 
	m_trigger = trigger;

	if (factory) {
		m_manager = factory->create<CommandManager>();
		if (trigger.isNull()) {
			m_trigger = m_manager->preferredTrigger();
		}
	} else m_manager = NULL;
}

QIcon Action::icon()
{
	if (m_manager) 
		return m_manager->icon();

	return QIcon(); 
}


Action::~Action()
{
	if (m_manager)
		delete m_manager;
}
