/*
 *  globals.cpp
 *
 *  Copyright (C) 2003, 2004, 2006, 2008 - Niko Ritari
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

#include "commont.h"
#include "debug.h"
#include "debugconfig.h"
#include "mutex.h"
#include "protocol.h"

// put here only those globals that don't have a module they naturally belong to; also keep globals to be eliminated in commont.cpp

// from commont.h
char directory_separator;
std::string wheregamedir;
volatile bool g_exitFlag;

// from debugconfig.h
AutoBugReporting g_autoBugReporting = ABR_disabled;
bool g_leetnetLog = false;
bool g_leetnetDataLog = false;

// from protocol.h
const std::string GAME_STRING = "Outgun";
const std::string GAME_PROTOCOL = "1.0";
const std::string REPLAY_IDENTIFICATION = "OUTGUNREPLAY";

// global objects whose construction/destruction relies on each other, and therefore need to be in one compilation unit to be constructed in the correct order
BareMutex g_threadLogMutex(BareMutex::NoLogging); // from debug.h
ThreadLog g_threadLog; // from debug.h
// Mutexes and ConditionVariables depend on the above
Mutex g_threadRandomSeedMutex("g_threadRandomSeedMutex"); // from thread.cpp
Mutex nlOpenMutex("network.cpp:nlOpenMutex"); // from network.cpp
