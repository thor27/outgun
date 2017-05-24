/*
 *  debugconfig.h
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

// Make Thread log its use in ./threadlog.bin.
static const bool LOG_THREAD_ACTIONS = false;

// Enable extra runtime checks on synchronization primives (also required for their LOG_*_ACTIONS to work).
#ifdef EXTRA_DEBUG
 #define DEBUG_SYNCHRONIZATION 1
#else
 #define DEBUG_SYNCHRONIZATION 0
#endif

#if DEBUG_SYNCHRONIZATION == 1

// Make synchronization primitives log their use in ./threadlog.bin. Generally you will also want LOG_THREAD_ACTIONS, to be able to identify the threads.
static const bool LOG_MUTEX_ACTIONS = false;
static const bool LOG_CONDVAR_ACTIONS = false;

#endif

// Flush ./threadlog.bin on every write to ensure everything is written in the event of a crash.
static const bool FLUSH_THREAD_LOG = false;

// Report on missing or wrongly ordered packets (both client and server).
static const bool WATCH_CONNECTION = false;

// Briefly log message types when receiving (both client and server; LEETNET_DATA_LOG does the same much better but the logs aren't as easy to read).
static const bool LOG_MESSAGE_TRAFFIC = false;

// What to report over the net in the event of an assertion.
enum AutoBugReporting { ABR_disabled, ABR_minimal, ABR_withDump };
extern AutoBugReporting g_autoBugReporting;


// If the LEETNET_* define is disabled, the feature can't be turned on; otherwise the activation depends on initialization in globals.cpp, and commandline args.

// Create leetclientlog.txt and leetserverlog.txt (useful mostly just for debugging Leetnet).
#define LEETNET_LOG
extern bool g_leetnetLog;

// Create leetclientdata.bin and leetserverdata.bin containing all network traffic of established connections (useful for any network debugging but the files may grow large).
#define LEETNET_DATA_LOG
extern bool g_leetnetDataLog;

// Create ./lnetdlog.bin containing timing information of Leetnet internals (not very useful and will quickly grow large).
//#define LEETNET_ACTIVITY_LOG

// A percentage of packets to lose on purpose; use this for simulating very poor network conditions.
#define LEETNET_SIMULATED_PACKET_LOSS 0

#endif
