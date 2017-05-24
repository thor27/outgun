/*
 *  mutex.h
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

#ifndef MUTEX_H_INC
#define MUTEX_H_INC

#include <errno.h>
#include "incpthread.h"
#include "debugconfig.h" // for DEBUG_SYNCHRONIZATION
#include "utility.h"
#include "nassert.h"

// note: don't use exit() (_exit() is OK) when a global Mutex may be locked

// BareMutex is only intended for mutexes referenced by Mutex code itself, or test cases wanting to avoid linking a heap of objects.
// Normally, use Mutex instead.
class BareMutex : private NoCopying, public Lockable {
    pthread_mutex_t mutex;
    friend class ConditionVariable;

public:
    enum LoggingDisabler { NoLogging };
    BareMutex(LoggingDisabler) throw ()                     { nAssert(0 == pthread_mutex_init(&mutex, 0)); }
    BareMutex(const char* identifier, bool = true) throw () { nAssert(0 == pthread_mutex_init(&mutex, 0)); nAssert(identifier); }
    ~BareMutex() throw () { nAssert(0 == pthread_mutex_destroy(&mutex)); }

    void lock() throw () { nAssert(0 == pthread_mutex_lock(&mutex)); }
    void unlock() throw () { nAssert(0 == pthread_mutex_unlock(&mutex)); }
};

#if DEBUG_SYNCHRONIZATION == 0

typedef BareMutex Mutex;

#elif DEBUG_SYNCHRONIZATION == 1

class Mutex : private NoCopying, public Lockable {
public:
    // Use Mutex m(NoLogging); rather than Mutex m("some-id", false); so that unlogged mutexes can be quicky found by text search (besides, identifier is never used in the latter case).
    // The logging_ parameter is intended to be used like Mutex m("some-id", SOME_SUBSYSTEM_EXTRA_LOGGING);.
    enum LoggingDisabler { NoLogging };
    Mutex(LoggingDisabler) throw ();
    Mutex(const char* identifier, bool logging_ = true) throw ();
    ~Mutex() throw ();

    void lock() throw ();
    void unlock() throw ();

private:
    pthread_mutex_t mutex;
    bool logging;
    bool locked;
    pthread_t owner; // only if locked
    int nWaiters;

    void logId(const char* identifier) throw ();
    void logAction(char operation) throw ();

    friend class ConditionVariable;
};

#else

#error DEBUG_SYNCHRONIZATION not properly set

#endif // DEBUG_SYNCHRONIZATION

#ifdef EXTRA_DEBUG

/** Assert mutual exclusion (only with EXTRA_DEBUG).
 * Verify mutual exclusion where it's supposed to be actually provided by other means (e.g. a real Mutex).
 * Trying to lock an AssertMutex while it's already locked will trigger an assertion.
 */
class AssertMutex : private NoCopying, public Lockable {
    Mutex mutex;
    bool locked;
    pthread_t owner;

public:
    AssertMutex() throw () : mutex(Mutex::NoLogging), locked(false) { }
    void lock() throw ();
    void unlock() throw ();
};

#else

class AssertMutex : private NoCopying, public Lockable {
public:
    void lock() throw () { }
    void unlock() throw () { }
};

#endif // EXTRA_DEBUG

#if DEBUG_SYNCHRONIZATION == 0

class ConditionVariable : private NoCopying {
    pthread_cond_t cond;

public:
    enum LoggingDisabler { NoLogging };
    ConditionVariable(LoggingDisabler) throw ()                     { nAssert(0 == pthread_cond_init(&cond, 0)); }
    ConditionVariable(const char* identifier, bool = true) throw () { nAssert(0 == pthread_cond_init(&cond, 0)); nAssert(identifier); }
    ~ConditionVariable() throw () { nAssert(0 == pthread_cond_destroy(&cond)); }

    void wait(Mutex& mutex) throw () { nAssert(0 == pthread_cond_wait(&cond, &mutex.mutex)); }
    bool timedWait(Mutex& mutex, const struct timespec& abstime) throw ();

    void signal   () throw () { nAssert(0 == pthread_cond_signal   (&cond)); }
    void broadcast() throw () { nAssert(0 == pthread_cond_broadcast(&cond)); }

    // note: these aren't generally needed even if you haven't already locked the mutex
    void signal   (Mutex& mutex) throw () { Lock ml(mutex); signal   (); }
    void broadcast(Mutex& mutex) throw () { Lock ml(mutex); broadcast(); }
};

#elif DEBUG_SYNCHRONIZATION == 1

class ConditionVariable : private NoCopying {
public:
    enum LoggingDisabler { NoLogging };
    ConditionVariable(LoggingDisabler) throw ();
    ConditionVariable(const char* identifier, bool logging_ = true) throw ();
    ~ConditionVariable() throw ();

    void wait(Mutex& mutex) throw ();
    bool timedWait(Mutex& mutex, const struct timespec& abstime) throw ();

    void signal() throw ();
    void broadcast() throw ();

    // note: these aren't generally needed even if you haven't already locked the mutex
    void signal   (Mutex& mutex) throw () { Lock ml(mutex); signal   (); }
    void broadcast(Mutex& mutex) throw () { Lock ml(mutex); broadcast(); }

private:
    pthread_cond_t cond;
    bool logging;
    int nWaiting;
    Mutex* waitingMutex; // only if nWaiting

    void debugPreWait(Mutex& mutex) throw ();
    void debugPostWait(Mutex& mutex) throw ();

    void logId(const char* identifier) throw ();
    void logAction(char operation) throw ();
};

#else

#error DEBUG_SYNCHRONIZATION not properly set

#endif // DEBUG_SYNCHRONIZATION

// Threadsafe: Wrapper of an object of type ObjT providing a thread safe very limited interface.
template<class ObjT> class Threadsafe : private NoCopying, public ConstLockable {
    mutable Mutex mutex;
    ObjT obj;

public:
    Threadsafe(const char* identifier) throw () : mutex(identifier) { }
    Threadsafe(const ObjT& o) throw () : obj(o) { }
    ~Threadsafe() throw () { }

    Threadsafe& operator=(const ObjT& o) throw () { mutex.lock(); obj = o; mutex.unlock(); return *this; }
    ObjT read() const throw () { mutex.lock(); ObjT o = obj; mutex.unlock(); return o; }  // Get a *copy* of the object

    // for more complex operations, use lock(), access() and unlock()
    void lock() const throw () { mutex.lock(); }
    void unlock() const throw () { mutex.unlock(); }
          ObjT& access() throw ()       { return obj; }  // use obj only between lock() and unlock()
    const ObjT& access() const throw () { return obj; }  // use obj only between lock() and unlock()
};

#endif
