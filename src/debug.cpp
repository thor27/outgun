/*
 *  debug.cpp
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

#include <cstdio>
#include <pthread.h>
#include "debug.h"
#include "debugconfig.h"
#include "log.h"
#include "mutex.h"
#include "thread.h"
#include "utility.h"

void MutexHolder::doLogAction(char operation) { // from mutex.h
    static MutexHolder logMutex;    // used to simplify its creation; using lock() or unlock() would lead in endless recursion
    nAssert(0 == pthread_mutex_lock(&logMutex.mutex));
    FILE* logFile = fopen("mutexlog.bin", "ab");
    if (logFile) {
        const pthread_t threadIdP = pthread_self();
        const int threadId = *reinterpret_cast<const int*>(&threadIdP);
        const int mutexId = reinterpret_cast<int>(&mutex);
        fwrite(&operation, sizeof(char), 1, logFile);
        fwrite(&threadId, sizeof(int), 1, logFile);
        fwrite(&mutexId, sizeof(int), 1, logFile);
        fclose(logFile);
    }
    nAssert(0 == pthread_mutex_unlock(&logMutex.mutex));
}

void logThreadEvent(bool exit, const char* function, Log& log) {
    if (LOG_THREAD_IDS) {
        const pthread_t threadIdP = pthread_self();
        log("%s%s() ID = %d, prio = %d", exit ? "exiting: " : "", function, *reinterpret_cast<const int*>(&threadIdP), Thread::getCallerPriority());
    }
}

void logThreadEvent(bool exit, const char* function, LogSet& log) {
    logThreadEvent(exit, function, *log.accessNormal());
}
