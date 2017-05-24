/*
 *  mutex.cpp
 *
 *  Copyright (C) 2008 - Niko Ritari
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

#include "debug.h"
#include "mutex.h"

#if DEBUG_SYNCHRONIZATION == 0

bool ConditionVariable::timedWait(Mutex& mutex, const struct timespec& abstime) throw () {
    const int val = pthread_cond_timedwait(&cond, &mutex.mutex, &abstime);
    nAssert(val == 0 || val == ETIMEDOUT);
    return val == ETIMEDOUT;
}

#elif DEBUG_SYNCHRONIZATION == 1

Mutex::Mutex(LoggingDisabler) throw () : logging(false), locked(false), nWaiters(0) {
    nAssert(0 == pthread_mutex_init(&mutex, 0));
}

Mutex::Mutex(const char* identifier, bool logging_) throw () : logging(logging_), locked(false), nWaiters(0) {
    Lock ml(g_threadLogMutex);
    nAssert(identifier);
    logId(identifier);
    logAction('C');
    nAssert(0 == pthread_mutex_init(&mutex, 0));
}

Mutex::~Mutex() throw () {
    Lock ml(g_threadLogMutex);
    logAction('D');
    nAssert(!locked && nWaiters == 0);
    nAssert(0 == pthread_mutex_destroy(&mutex));
}

void Mutex::lock() throw () {
    {
        Lock ml(g_threadLogMutex);
        logAction('L');
        nAssert(!locked || owner != pthread_self());
        ++nWaiters;
    }
    nAssert(0 == pthread_mutex_lock(&mutex));
    {
        Lock ml(g_threadLogMutex);
        --nWaiters;
        nAssert(!locked);
        locked = true;
        owner = pthread_self();
        logAction('G');
    }
}

void Mutex::unlock() throw () {
    {
        Lock ml(g_threadLogMutex);
        logAction('U');
        nAssert(locked && owner == pthread_self());
        locked = false;
    }
    nAssert(0 == pthread_mutex_unlock(&mutex));
}

void Mutex::logId(const char* identifier) throw () {
    if (!LOG_MUTEX_ACTIONS || !logging)
        return;
    ThreadLogWriter t(g_threadLog);
    t.put('M');
    t.putObjectId(this);
    t.put('I');
    t.put(identifier);
}

void Mutex::logAction(char operation) throw () {
    if (!LOG_MUTEX_ACTIONS || !logging)
        return;
    ThreadLogWriter t(g_threadLog);
    t.put('M');
    t.putObjectId(this);
    t.put(operation);
}

ConditionVariable::ConditionVariable(LoggingDisabler) throw () : logging(false), nWaiting(0) {
    nAssert(0 == pthread_cond_init(&cond, 0));
}

ConditionVariable::ConditionVariable(const char* identifier, bool logging_) throw () : logging(logging_), nWaiting(0) {
    Lock ml(g_threadLogMutex);
    nAssert(identifier);
    logId(identifier);
    logAction('C');
    nAssert(0 == pthread_cond_init(&cond, 0));
}

ConditionVariable::~ConditionVariable() throw () {
    Lock ml(g_threadLogMutex);
    logAction('D');
    nAssert(nWaiting == 0);
    nAssert(0 == pthread_cond_destroy(&cond));
}

void ConditionVariable::wait(Mutex& mutex) throw () {
    debugPreWait(mutex);
    nAssert(0 == pthread_cond_wait(&cond, &mutex.mutex));
    debugPostWait(mutex);
}

bool ConditionVariable::timedWait(Mutex& mutex, const struct timespec& abstime) throw () {
    debugPreWait(mutex);
    const int val = pthread_cond_timedwait(&cond, &mutex.mutex, &abstime);
    nAssert(val == 0 || val == ETIMEDOUT);
    debugPostWait(mutex);
    return val == ETIMEDOUT;
}

void ConditionVariable::signal() throw () {
    {
        Lock ml(g_threadLogMutex);
        logAction('S');
    }
    nAssert(0 == pthread_cond_signal(&cond));
}

void ConditionVariable::broadcast() throw () {
    {
        Lock ml(g_threadLogMutex);
        logAction('B');
    }
    nAssert(0 == pthread_cond_broadcast(&cond));
}

void ConditionVariable::debugPreWait(Mutex& mutex) throw () {
    Lock ml(g_threadLogMutex);
    mutex.logAction('W');
    logAction('W');
    nAssert(nWaiting == 0 || waitingMutex == &mutex);
    ++nWaiting;
    waitingMutex = &mutex;
    nAssert(mutex.locked && mutex.owner == pthread_self());
    mutex.locked = false;
}

void ConditionVariable::debugPostWait(Mutex& mutex) throw () {
    Lock ml(g_threadLogMutex);
    nAssert(!mutex.locked);
    mutex.locked = true;
    mutex.owner = pthread_self();
    --nWaiting;
    logAction('w');
    mutex.logAction('w');
}

void ConditionVariable::logId(const char* identifier) throw () {
    if (!LOG_CONDVAR_ACTIONS || !logging)
        return;
    ThreadLogWriter t(g_threadLog);
    t.put('C');
    t.putObjectId(this);
    t.put('I');
    t.put(identifier);
}

void ConditionVariable::logAction(char operation) throw () {
    if (!LOG_CONDVAR_ACTIONS || !logging)
        return;
    ThreadLogWriter t(g_threadLog);
    t.put('C');
    t.putObjectId(this);
    t.put(operation);
}

#else

#error DEBUG_SYNCHRONIZATION not properly set

#endif

#ifdef EXTRA_DEBUG

void AssertMutex::lock() throw () {
    Lock ml(mutex);
    nAssert(!locked);
    locked = true;
    owner = pthread_self();
}

void AssertMutex::unlock() throw () {
    Lock ml(mutex);
    nAssert(locked && owner == pthread_self());
    locked = false;
}

#endif
