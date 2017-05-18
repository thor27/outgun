/*
 *  debugconfig.h
 *
 *  Copyright (C) 2004 - Niko Ritari
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

#ifndef DEBUGCONFIG_H_INC
#define DEBUGCONFIG_H_INC

// General settings:

// make all thread functions log their start and exit to the context's regular log file; helps reading any dumps that contain thread IDs (e.g. mutex log)
static const bool LOG_THREAD_IDS = false;

// create ./mutexlog.bin containing information on every lock and unlock operation on a MutexHolder, including thread ID
static const bool LOG_MUTEX_LOCKUNLOCK = false;

// report on missing or wrongly ordered packets (both client and server)
static const bool WATCH_CONNECTION = false;

// briefly log message types when receiving (both client and server; LEETNET_DATA_LOG does the same much better but the logs aren't as easy to read)
static const bool LOG_MESSAGE_TRAFFIC = false;

// what to report over the net in the event of an assertion
enum AutoBugReporting { ABR_disabled, ABR_minimal, ABR_withDump };
extern AutoBugReporting g_autoBugReporting;


// Leetnet specific settings:

// if the LEETNET_* define is disabled, the feature can't be turned on; otherwise the activation depends on initialization in globals.cpp, and commandline args

// create leetclientlog.txt and leetserverlog.txt (useful mostly just for debugging Leetnet)
#define LEETNET_LOG
extern bool g_leetnetLog;

// create leetclientdata.bin and leetserverdata.bin containing all network traffic of established connections (useful for any network debugging but the files may grow large)
#define LEETNET_DATA_LOG
extern bool g_leetnetDataLog;

// create ./lnetdlog.bin containing timing information of Leetnet internals (not very useful and will quickly grow large)
//#define LEETNET_ACTIVITY_LOG

// a percentage of packets to lose on purpose; use this for simulating very poor network conditions
#define LEETNET_SIMULATED_PACKET_LOSS 0

#endif
