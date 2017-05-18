/*
 *  thread.cpp
 *
 *  Copyright (C) 2004, 2006 - Niko Ritari
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
#include <pthread.h>
#include "language.h"
#include "mutex.h"
#include "thread.h"
#include "utility.h"

MutexHolder g_randomSeedMutex;
time_t g_randomSeed = time(0);

void Thread::randomize() {
    MutexLock ml(g_randomSeedMutex);
    srand(++g_randomSeed);
}

int Thread::doStart(pthread_t* pThread, void* (*function)(void*), void* argument, bool detached, int priority) {
    pthread_attr_t attr;
    int val = pthread_attr_init(&attr);

    if (val != 0)
        return val;
    nAssert(0 == pthread_attr_setdetachstate(&attr, detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE));

    sched_param param;
    param.sched_priority = priority;
    nAssert(0 == pthread_attr_setschedparam(&attr, &param));

    val = pthread_create(pThread, &attr, function, argument);

    nAssert(0 == pthread_attr_destroy(&attr));

    return val;
}

void Thread::doSetPriority(pthread_t thread, int priority) {
    int policy;
    sched_param param;
    nAssert(0 == pthread_getschedparam(thread, &policy, &param));
    param.sched_priority = priority;
    nAssert(0 == pthread_setschedparam(thread, policy, &param));
}

int Thread::doGetPriority(pthread_t thread) {
    int policy;
    sched_param param;
    nAssert(0 == pthread_getschedparam(thread, &policy, &param));
    return param.sched_priority;
}

void Thread::startError() {
    criticalError(_("Can't create new thread. Insufficient system resources."));
}

void Thread::join(bool acceptRecursive) {
    nAssert(running);
    running = false;
    int ret = pthread_join(thread, 0);
    numAssert(ret == 0 || (acceptRecursive && ret == EDEADLK), ret);
}

void Thread::detach() {
    nAssert(running);
    running = false;
    nAssert(0 == pthread_detach(thread));
}
