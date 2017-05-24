/*
 *  tests.h
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

#ifndef TESTS_TESTS_H
#define TESTS_TESTS_H

#include "../incpthread.h"

#include "../nassert.h"

/* Testing assertions has the obvious problem of missed cleanup, since execution stops at the failed assertion.
 * Where this is a problem, either give up testing the assertion, perform the cleanup manually after the assertion,
 * or arrange so that no tests that would require cleanup are executed after the assertion test. Note that cleanup
 * might be required just to destroy objects used in the test.
 */

extern bool expectAssertion;

template<class Function> void* assertionTestThreadMain(void* arg) throw () {
    Function* fn = static_cast<Function*>(arg);
    expectAssertion = true;
    (*fn)();
    // if we get here, the test failed to trigger the assert it was supposed to
    expectAssertion = false;
    nAssert(0);
}

template<class Function> void testAssertion(Function fn) throw () {
    pthread_t thread;
    const bool ok = pthread_create(&thread, 0, assertionTestThreadMain<Function>, &fn) == 0 && pthread_join(thread, 0) == 0;
    expectAssertion = false;
    nAssert(ok);
}

class SimpleThread {
    pthread_t thread;
    bool running;

public:
    SimpleThread() throw () : running(false) { }
    ~SimpleThread() throw () { nAssert(!running); }

    void start(void* (*fn)(void*), void* arg) throw () {
        nAssert(!running);
        running = true;
        nAssert(pthread_create(&thread, 0, fn, arg) == 0);
    }
    void* join() throw () {
        nAssert(running);
        void* value;
        nAssert(pthread_join(thread, &value) == 0);
        running = false;
        return value;
    }
    void detach() throw () {
        nAssert(running);
        running = false;
        nAssert(pthread_detach(thread) == 0);
    }
};

#endif
