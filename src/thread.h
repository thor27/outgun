/*
 *  thread.h
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

#ifndef THREAD_H_INC
#define THREAD_H_INC

#include <errno.h>

#include "incpthread.h"
#include "nassert.h"    // for STACK_GUARD and __attribute__ for non-GCC, as well as nAssert
#include "utility.h"

class Thread : private NoCopying {
public:
    Thread() throw () : running(false) { }
    ~Thread() throw () { nAssert(!running); }

    /* The identity argument is (only) used to identify the new thread in threadlog.bin if LOG_THREAD_ACTIONS is enabled.
     * If identity is null, the identity is not logged, but all actions on the thread still will be.
     * To be useful, the identity information should indicate at least the thread function and perhaps the owner.
     */
    template<class Function>        int  start                     (const char* identity, Function fun, int priority) throw ();
    template<class Function>        void start_assert              (const char* identity, Function fun, int priority) throw ();
    template<class Function> static int  startDetachedThread       (const char* identity, Function fun, int priority) throw ();
    template<class Function> static void startDetachedThread_assert(const char* identity, Function fun, int priority) throw ();

    template<class Function, class ArgumentT>        int  start                     (const char* identity, Function fun, ArgumentT arg, int priority) throw ();
    template<class Function, class ArgumentT>        void start_assert              (const char* identity, Function fun, ArgumentT arg, int priority) throw ();
    template<class Function, class ArgumentT> static int  startDetachedThread       (const char* identity, Function fun, ArgumentT arg, int priority) throw ();
    template<class Function, class ArgumentT> static void startDetachedThread_assert(const char* identity, Function fun, ArgumentT arg, int priority) throw ();

    bool isRunning() const throw () { return running; } // note: this tells if there's need for join or detach rather than if the thread is active
    void join(bool acceptRecursive = false) throw ();
    void detach() throw ();
    void setPriority(int priority) throw () { nAssert(running); doSetPriority(thread, priority); }
    int getPriority() const throw () { nAssert(running); return doGetPriority(thread); }

    static void setCallerPriority(int priority) throw () { doSetPriority(pthread_self(), priority); }
    static int getCallerPriority() throw () { return doGetPriority(pthread_self()); }

    static void logCallerIdentity(const char* identity) throw () { nAssert(identity); logEvent(pthread_self(), 'I', identity); }

private:
    static void randomize() throw (); // calls srand with an unique seed, to help on implementations on which each thread has its own random seed
    static int doStart(pthread_t* pThread, const char* identity, void* (*function)(void*), void* argument, bool detached, int priority) throw ();
    static void assertStartSuccess(int returned) throw (); // call with return value from any start function; terminates process with an appropriate error message if start wasn't successful
    static int doGetPriority(pthread_t thread) throw ();
    static void doSetPriority(pthread_t thread, int priority) throw ();
    static void logEvent(pthread_t thread, char event, const char* data = 0) throw ();

    template<class Function> struct ThreadData0 {
        Function function;
        ThreadData0(Function fun) throw () : function(fun) { }
    };
    template<class Function> static void* starter0(void* pv_arg) throw ();
    template<class Function> static int doStart0(pthread_t* pThread, const char* identity, Function fun, bool detached, int priority) throw ();

    template<class Function, class ArgumentT> struct ThreadData1 {
        Function function;
        ArgumentT arg;
        ThreadData1(Function fun, ArgumentT arg_) throw () : function(fun), arg(arg_) { }
    };
    template<class Function, class ArgumentT> static void* starter1(void* pv_arg) throw ();
    template<class Function, class ArgumentT> static int doStart1(pthread_t* pThread, const char* identity, Function fun, ArgumentT arg, bool detached, int priority) throw ();

    pthread_t thread;
    bool running; // set if running AND not detached
};

// template implementation

template<class Function> int Thread::start(const char* identity, Function fun, int priority) throw () {
    nAssert(!running);
    running = true;
    return doStart0(&thread, identity, fun, false, priority);
}

template<class Function, class ArgumentT> int Thread::start(const char* identity, Function fun, ArgumentT arg, int priority) throw () {
    nAssert(!running);
    running = true;
    return doStart1(&thread, identity, fun, arg, false, priority);
}

template<class Function> void Thread::start_assert(const char* identity, Function fun, int priority) throw () {
    assertStartSuccess(start(identity, fun, priority));
}

template<class Function, class ArgumentT> void Thread::start_assert(const char* identity, Function fun, ArgumentT arg, int priority) throw () {
    assertStartSuccess(start(identity, fun, arg, priority));
}

template<class Function> int Thread::startDetachedThread(const char* identity, Function fun, int priority) throw () {
    pthread_t tthread;
    return doStart0(&tthread, identity, fun, true, priority);
}

template<class Function, class ArgumentT> int Thread::startDetachedThread(const char* identity, Function fun, ArgumentT arg, int priority) throw () {
    pthread_t tthread;
    return doStart1(&tthread, identity, fun, arg, true, priority);
}

template<class Function> void Thread::startDetachedThread_assert(const char* identity, Function fun, int priority) throw () {
    assertStartSuccess(startDetachedThread(identity, fun, priority));
}

template<class Function, class ArgumentT> void Thread::startDetachedThread_assert(const char* identity, Function fun, ArgumentT arg, int priority) throw () {
    assertStartSuccess(startDetachedThread(identity, fun, arg, priority));
}

template<class Function> void* Thread::starter0(void* pv_arg) throw () {
    uint32_t stackGuard = STACK_GUARD; stackGuardHackPtr = &stackGuard;
    logEvent(pthread_self(), 'R');
    randomize();
    ThreadData0<Function>* tdata = static_cast<ThreadData0<Function>*>(pv_arg);
    tdata->function();
    delete tdata;
    logEvent(pthread_self(), 'E');
    return 0;
}

template<class Function> int Thread::doStart0(pthread_t* pThread, const char* identity, Function fun, bool detached, int priority) throw () {
    ThreadData0<Function>* td = new ThreadData0<Function>(fun);
    const int val = doStart(pThread, identity, starter0<Function>, td, detached, priority);
    if (val != 0) // couldn't start starter0 that would handle the deletion of td
        delete td;
    return val;
}

template<class Function, class ArgumentT> void* Thread::starter1(void* pv_arg) throw () {
    uint32_t stackGuard = STACK_GUARD; stackGuardHackPtr = &stackGuard;
    logEvent(pthread_self(), 'R');
    randomize();
    ThreadData1<Function, ArgumentT>* tdata = static_cast<ThreadData1<Function, ArgumentT>*>(pv_arg);
    tdata->function(tdata->arg);
    delete tdata;
    logEvent(pthread_self(), 'E');
    return 0;
}

template<class Function, class ArgumentT> int Thread::doStart1(pthread_t* pThread, const char* identity, Function fun, ArgumentT arg, bool detached, int priority) throw () {
    ThreadData1<Function, ArgumentT>* td = new ThreadData1<Function, ArgumentT>(fun, arg);
    const int val = doStart(pThread, identity, starter1<Function, ArgumentT>, td, detached, priority);
    if (val != 0)   // couldn't start starter1 that would handle the deletion of td
        delete td;
    return val;
}

#endif
