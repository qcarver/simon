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


#ifndef ACTIONMODEL_H
#define ACTIONMODEL_H
#include <QList>
#include <QAbstractItemModel>
#include "simonmodelmanagement_export.h"

class Action;

class MODELMANAGEMENT_EXPORT ActionModel : public QAbstractItemModel {

protected:
	QList<Action*> m_actions;

	Qt::ItemFlags flags(const QModelIndex &index) const;
	QVariant headerData(int, Qt::Orientation orientation,
				int role = Qt::DisplayRole) const;
	QModelIndex parent(const QModelIndex &index) const;
	int rowCount(const QModelIndex &parent = QModelIndex()) const;
	QModelIndex index(int row, int column,const QModelIndex &parent = QModelIndex()) const;
	virtual QVariant data(const QModelIndex &index, int role) const;
	virtual int columnCount(const QModelIndex &parent = QModelIndex()) const;


public:
	ActionModel(QObject *parent=0);

	bool addAction(Action*);
	bool clearActions();
	bool takeAction(Action*);

	virtual ~ActionModel();

};

#endif

