/*
 *  thread.cpp
 *
 *  Copyright (C) 2004, 2006, 2008 - Niko Ritari
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

#include <cstdlib>
#include <ctime>
#include <cerrno>

#include "debug.h"
#include "language.h"
#include "mutex.h"
#include "thread.h"
#include "utility.h"

extern Mutex g_threadRandomSeedMutex;
static time_t g_randomSeed = time(0);

void Thread::randomize() throw () {
    Lock ml(g_threadRandomSeedMutex);
    srand(++g_randomSeed);
}

int Thread::doStart(pthread_t* pThread, const char* identity, void* (*function)(void*), void* argument, bool detached, int priority) throw () {
    pthread_attr_t attr;
    int val = pthread_attr_init(&attr);
    if (val != 0)
        return val;
    nAssert(0 == pthread_attr_setdetachstate(&attr, detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE));

    sched_param param;
    param.sched_priority = priority;
    nAssert(0 == pthread_attr_setschedparam(&attr, &param));

    val = pthread_create(pThread, &attr, function, argument);
    logEvent(*pThread, 'C', ((detached ? 'D' : 'J') + itoa(priority)).c_str());
    if (identity)
        logEvent(*pThread, 'I', identity);

    nAssert(0 == pthread_attr_destroy(&attr));

    return val;
}

void Thread::assertStartSuccess(int returned) throw () {
    if (returned == EAGAIN || returned == ENOMEM)
        criticalError(_("Can't create new thread. Insufficient system resources."));
    numAssert(returned == 0, returned);
}

void Thread::doSetPriority(pthread_t thread, int priority) throw () {
    logEvent(thread, 'P', itoa(priority).c_str());
    int policy;
    sched_param param;
    nAssert(0 == pthread_getschedparam(thread, &policy, &param));
    param.sched_priority = priority;
    nAssert(0 == pthread_setschedparam(thread, policy, &param));
}

int Thread::doGetPriority(pthread_t thread) throw () {
    int policy;
    sched_param param;
    nAssert(0 == pthread_getschedparam(thread, &policy, &param));
    return param.sched_priority;
}

void Thread::logEvent(pthread_t thread, char event, const char* data) throw () {
    if (!LOG_THREAD_ACTIONS)
        return;
    Lock ml(g_threadLogMutex);
    ThreadLogWriter t(g_threadLog);
    t.put('T');
    t.putThreadId(thread);
    t.put(event);
    if (data)
        t.put(data);
}

void Thread::join(bool acceptRecursive) throw () {
    nAssert(running);
    running = false;
    int ret;
    if (pthread_equal(thread, pthread_self())) {
        nAssert(acceptRecursive);
        logEvent(thread, 'd');
        ret = pthread_detach(thread);
    }
    else {
        logEvent(thread, 'J');
        ret = pthread_join(thread, 0);
        logEvent(thread, 'j');
    }
    numAssert(ret == 0, ret);
}

void Thread::detach() throw () {
    nAssert(running);
    running = false;
    logEvent(thread, 'D');
    nAssert(0 == pthread_detach(thread));
}
