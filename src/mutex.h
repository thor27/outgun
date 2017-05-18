/*
 *  mutex.h
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

#ifndef MUTEX_H_INC
#define MUTEX_H_INC

#include <pthread.h>
#include "debugconfig.h"    // for LOG_MUTEX_LOCKUNLOCK
#include "nassert.h"
#include "utility.h"

// note: don't use exit() (_exit() is OK) when a global MutexHolder may be locked
class MutexHolder {
    pthread_mutex_t mutex;

public:
    MutexHolder()  { nAssert(0 == pthread_mutex_init(&mutex, 0)); }
    ~MutexHolder() { nAssert(0 == pthread_mutex_destroy(&mutex)); }
    void lock()    { logAction('L'); nAssert(0 == pthread_mutex_lock(&mutex)); logAction('G'); }
    void unlock()  { logAction('U'); nAssert(0 == pthread_mutex_unlock(&mutex)); }

private:
    void doLogAction(char operation);   // defined in debug.cpp
    void logAction(char operation) {
        if (LOG_MUTEX_LOCKUNLOCK)
            doLogAction(operation);
    }
};

class MutexLock {
    MutexHolder& mutex;

public:
    MutexLock(MutexHolder& mutex_) : mutex(mutex_) { mutex.lock(); }
    ~MutexLock() { mutex.unlock(); }
};

// Threadsafe: Wrapper of an object of type ObjT providing a thread safe very limited interface.
template<class ObjT>
class Threadsafe {
    mutable MutexHolder mutex;
    volatile ObjT obj;

public:
    Threadsafe() { }
    Threadsafe(const ObjT& o) : obj(o) { }

    Threadsafe& operator=(const ObjT& o) { mutex.lock(); volatile_ref_cast<ObjT>(obj) = o; mutex.unlock(); return *this; }
    ObjT read() const { mutex.lock(); ObjT o = volatile_ref_cast<const ObjT>(obj); mutex.unlock(); return o; }  // Get a *copy* of the object

    // for more complex operations, use lock(), access() and unlock()
    void lock() const { mutex.lock(); }
    void unlock() const { mutex.unlock(); }
          ObjT& access()       { return obj; }  // use obj only between lock() and unlock()
    const ObjT& access() const { return obj; }  // use obj only between lock() and unlock()
};

extern MutexHolder nlOpenMutex;

#endif
