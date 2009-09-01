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

#ifndef ACTION_H
#define ACTION_H

#include <QString>
#include <QObject>
#include <QPointer>
#include <QIcon>
#include <KLocalizedString>
#include <QDomElement>
#ifdef SIMON_SCENARIOS
#include <simonscenariobase/scenarioobject.h>
#endif
#include "actionlib_export.h"
class CommandManager;
class VersionNumber;

#ifdef SIMON_SCENARIOS
class SIMONACTIONS_EXPORT Action : public QObject, public ScenarioObject
#else
class SIMONACTIONS_EXPORT Action : public QObject
#endif
{
	Q_OBJECT
	private:
		QString m_source;
		QString m_trigger;
		QString m_version;
		VersionNumber *pluginMinVersion;
		VersionNumber *pluginMaxVersion;
		QPointer<CommandManager> m_manager;
		bool m_enabledByDefault;

		void init(const QString& source, const QString& trigger=QString());
		#ifdef SIMON_SCENARIOS
		Action(const QString& scenarioId, const QString& source, const QString& trigger);
		#endif
	public:
		Action(const QString& source, const QString& trigger=QString());
		typedef QPointer<Action> Ptr;

		#ifdef SIMON_SCENARIOS
		static Action* createAction(const QString& scenarioId, const QDomElement& elem);
		#endif

		bool enabledByDefault() { return m_enabledByDefault; }
		QString source() { return m_source; }
		QString trigger() { return m_trigger; }

		QString getPluginVersion() { return m_version; }

		QIcon icon();
		QPointer<CommandManager> manager() { return m_manager; }
		void setTrigger(const QString& newTrigger) { m_trigger=newTrigger; }

		#ifdef SIMON_SCENARIOS
		bool deSerialize(const QDomElement&);
		QDomElement serialize(QDomDocument *doc);
		#endif

		~Action();

};

#endif
