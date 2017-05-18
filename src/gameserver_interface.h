/*
 *  gameserver_interface.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005 - Niko Ritari
 *  Copyright (C) 2003, 2004 - Jani Rivinoja
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef GAMESERVER_INTERFACE_INC
#define GAMESERVER_INTERFACE_INC

#include <string>
#include "commont.h"
#include "utility.h"

class Server;
class LogSet;
class MemoryLog;

class ServerExternalSettings {
public:
    bool dedserver;     // dedicated server? only affects what's told to master and asking players
    int port;           // the server port
    int minLocalPort, maxLocalPort; // secondary ports
    bool privateserver; // private server?
    std::string ipAddress;
    // forced means set from the outside so that the server should not change them according to lesser priority requests
    bool portForced;
    bool privSettingForced;
    bool ipForced;
    int server_maxplayers;  //maxplayers for the local server, given on the command line (don't use anywhere new)
    int lowerPriority, priority, networkPriority;   // lower is used for non-timecritical background threads; all must be set properly when used
    bool threadLock;    // disable all concurrency?

    typedef void StatusOutputFnT(const std::string& str);
    StatusOutputFnT* statusOutput;  // must be set properly (non-null) when used
    bool showErrorCount;
    bool ownScreen;

    ServerExternalSettings() : dedserver(false), port(DEFAULT_UDP_PORT), minLocalPort(0), maxLocalPort(0), privateserver(false),
        portForced(false), privSettingForced(false), ipForced(false), server_maxplayers(16), threadLock(true), statusOutput(0), showErrorCount(true), ownScreen(false) { }
};

class GameserverInterface {
    Server* host;

public:
    GameserverInterface(LogSet& hostLog, const ServerExternalSettings& settings, Log& externalErrorLog, const std::string& errorPrefix);  // externalErrorLog must outlive the Server object
    ~GameserverInterface();
    bool start(int maxplayers);
    void loop(volatile bool *quitFlag, bool quitOnEsc);
    void stop();
};

// implementation is in server.cpp

#endif
