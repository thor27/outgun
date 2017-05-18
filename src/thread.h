/*
 *  thread.h
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

#ifndef THREAD_H_INC
#define THREAD_H_INC

#include <pthread.h>
#include "errno.h"
#include "nassert.h"    // for STACK_GUARD and __attribute__ for non-GCC, as well as nAssert

class Thread {
    static void randomize();    // calls srand with an unique seed
    static int doStart(pthread_t* pThread, void* (*function)(void*), void* argument, bool detached, int priority);
    static int doGetPriority(pthread_t thread);
    static void doSetPriority(pthread_t thread, int priority);

    template<class Function>
    struct ThreadData0 {
        Function function;
        ThreadData0(Function fun) : function(fun) { }
    };
    template<class Function>
    static void* starter0(void* pv_arg) {
        unsigned long stackGuard = STACK_GUARD; stackGuardHackPtr = &stackGuard;
        randomize();    // help for implementations on which each thread has it's own random seed
        ThreadData0<Function>* tdata = static_cast<ThreadData0<Function>*>(pv_arg);
        tdata->function();
        delete tdata;
        return 0;
    }
    template<class Function>
    static int doStart0(pthread_t* pThread, Function fun, bool detached, int priority) {
        ThreadData0<Function>* td = new ThreadData0<Function>(fun);
        int val = doStart(pThread, starter0<Function>, td, detached, priority);
        if (val != 0)   // couldn't start starter0 that would handle the deletion of td
            delete td;
        return val;
    }

    template<class Function, class ArgumentT>
    struct ThreadData1 {
        Function function;
        ArgumentT arg;
        ThreadData1(Function fun, ArgumentT arg_) : function(fun), arg(arg_) { }
    };
    template<class Function, class ArgumentT>
    static void* starter1(void* pv_arg) {
        unsigned long stackGuard = STACK_GUARD; stackGuardHackPtr = &stackGuard;
        randomize();    // help for implementations on which each thread has it's own random seed
        ThreadData1<Function, ArgumentT>* tdata = static_cast<ThreadData1<Function, ArgumentT>*>(pv_arg);
        tdata->function(tdata->arg);
        delete tdata;
        return 0;
    }
    template<class Function, class ArgumentT>
    static int doStart1(pthread_t* pThread, Function fun, ArgumentT arg, bool detached, int priority) {
        ThreadData1<Function, ArgumentT>* td = new ThreadData1<Function, ArgumentT>(fun, arg);
        int val = doStart(pThread, starter1<Function, ArgumentT>, td, detached, priority);
        if (val != 0)   // couldn't start starter1 that would handle the deletion of td
            delete td;
        return val;
    }

    static void startError() __attribute__ ((noreturn)); // exit with error message, for problems starting a thread

    pthread_t thread;
    bool running;   // set if running AND not detached

    // deny copying (these aren't defined anywhere)
    Thread(const Thread&);
    Thread& operator=(const Thread&);

public:
    Thread() : running(false) { }
    ~Thread() { nAssert(!running); }

    template<class Function>
    static int startDetachedThread(Function fun, int priority) {
        pthread_t tthread;
        return doStart0(&tthread, fun, true, priority);
    }
    template<class Function>
    static void startDetachedThread_assert(Function fun, int priority) { int val = startDetachedThread(fun, priority); if (val == EAGAIN || val == ENOMEM) startError(); numAssert(val == 0, val); }

    template<class Function, class ArgumentT>
    static int startDetachedThread(Function fun, ArgumentT arg, int priority) {
        pthread_t tthread;
        return doStart1(&tthread, fun, arg, true, priority);
    }
    template<class Function, class ArgumentT>
    static void startDetachedThread_assert(Function fun, ArgumentT arg, int priority) { int val = startDetachedThread(fun, arg, priority); if (val == EAGAIN || val == ENOMEM) startError(); numAssert(val == 0, val); }

    template<class Function>
    int start(Function fun, int priority) {
        nAssert(!running);
        running = true;
        return doStart0(&thread, fun, false, priority);
    }
    template<class Function>
    void start_assert(Function fun, int priority) { int val = start(fun, priority); if (val == EAGAIN || val == ENOMEM) startError(); numAssert(val == 0, val); }

    template<class Function, class ArgumentT>
    int start(Function fun, ArgumentT arg, int priority) {
        nAssert(!running);
        running = true;
        return doStart1(&thread, fun, arg, false, priority);
    }
    template<class Function, class ArgumentT>
    void start_assert(Function fun, ArgumentT arg, int priority) { int val = start(fun, arg, priority); if (val == EAGAIN || val == ENOMEM) startError(); numAssert(val == 0, val); }

    bool isRunning() const { return running; }  // note: this tells if there's need for join or detach rather than if the thread is active
    void join(bool acceptRecursive = false);
    void detach();
    void setPriority(int priority) { nAssert(running); doSetPriority(thread, priority); }
    int getPriority() const { nAssert(running); return doGetPriority(thread); }

    static void setCallerPriority(int priority) { doSetPriority(pthread_self(), priority); }
    static int getCallerPriority() { return doGetPriority(pthread_self()); }
};

#endif
