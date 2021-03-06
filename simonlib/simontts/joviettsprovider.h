/*
 *   Copyright (C) 2010 Peter Grasch <peter.grasch@bedahr.org>
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

#ifndef SIMON_JovieTTSProvider_H_7A7B9100FF5245329569C1B540119C37
#define SIMON_JovieTTSProvider_H_7A7B9100FF5245329569C1B540119C37

#include "simonttsprovider.h"

class QString;
class QDBusInterface;

/**
 * \class JovieTTSProvider
 * \author Peter Grasch
 * \since 0.4
 * \brief Interface to Jovie - the KDE text to speech system
 */
class JovieTTSProvider : public SimonTTSProvider
{
  private:
    /**
     * \brief The interface to talk to; May be 0 if not yet initialized
     *
     * The initialization is performed each time \sa say() or \sa initialize()
     * is called for as long as this remains 0 
     */
    QDBusInterface *interface;

  public:
    JovieTTSProvider();
    bool initialize();
    bool uninitialize();
    bool canSay(const QString& text);
    bool say(const QString& text);
    bool interrupt();
    ~JovieTTSProvider();

};

#endif


