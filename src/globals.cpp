/*
 *  globals.cpp
 *
 *  Copyright (C) 2003, 2004, 2006 - Niko Ritari
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
#include "debugconfig.h"
#include "mutex.h"
#include "timer.h"

// put here only those globals that don't have a module they naturally belong to; also keep globals to be eliminated in commont.cpp

// from commont.h
char directory_separator;
std::string wheregamedir;

// from timer.h
SystemTimer* g_systemTimer = 0;
TimeCounter g_timeCounter;

// from mutex.h
MutexHolder nlOpenMutex;

// from debugconfig.h
AutoBugReporting g_autoBugReporting = ABR_disabled;
bool g_leetnetLog = false;
bool g_leetnetDataLog = false;
