//
// C++ Interface: textmacro
//
// Description: 
//
//
// Author: Peter Grasch <bedahr@gmx.net>, (C) 20078
//
// Copyright: See COPYING file that comes with this distribution
//
//
#ifndef TEXTMACRO_H
#define TEXTMACRO_H


#include "../command.h"


/**
 *	@class TextMacroCommand
 *	@brief Ressembles one tectmacro
 *
 *	@version 0.1
 *	@date 20.05.2008
 *	@author Peter Grasch
 */
class TextMacroCommand : public Command {

private:
	QString text;

protected:
	bool triggerPrivate();
	const QMap<QString,QVariant> getValueMapPrivate() const;

public:
	static const QString staticCategoryText();
	static const QIcon staticCategoryIcon();

	const QIcon getCategoryIcon() const;
	const QString getCategoryText() const;

	
    /**
    * @brief Constructor
    * 
    *	@author Peter Grasch
    */
    TextMacroCommand(QString name, QString iconSrc, QString text) : Command(name, iconSrc)
    {
        this->text = text;
    }

    /**
    * @brief Returns the text
    * 
    *	@author Peter Grasch
    */
    const QString getText() const { return this->text; }
    

	void change(QString newName, QString newIconSrc, QString newText) { 
		this->text = newText;
		Command::change(newName, newIconSrc);
	}
    
    ~TextMacroCommand() {}

};

#endif