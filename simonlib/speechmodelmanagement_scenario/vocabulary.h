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


#ifndef VOCABULARY_H
#define VOCABULARY_H
#include <QString>
#include <QList>
#include <QAbstractItemModel>

#include "simonmodelmanagement_export.h"

#include <speechmodelbase_scenario/word.h>
#include <simonscenariobase/scenarioobject.h>

class MODELMANAGEMENT_EXPORT Vocabulary : public ScenarioObject, public QAbstractItemModel
{

private:
	Qt::ItemFlags flags(const QModelIndex &index) const;
	QVariant headerData(int, Qt::Orientation orientation,
				int role = Qt::DisplayRole) const;
	QModelIndex index(int row, int column,
			const QModelIndex &parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex &index) const;
	int rowCount(const QModelIndex &parent = QModelIndex()) const;
	int columnCount(const QModelIndex &parent = QModelIndex()) const;


protected:
	Vocabulary(Scenario *parent);
	QList<Word*> m_words;
	virtual QVariant data(const QModelIndex &index, int role) const;

public:
	static Vocabulary* createVocabulary(Scenario *parent, const QDomElement&);
	bool deSerialize(const QDomElement&);
	QDomElement serialize(QDomDocument *doc);

	bool removeWord(Word* w);

	int wordCount() { return m_words.count(); }
	virtual ~Vocabulary() {}
};

#endif
