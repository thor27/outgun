/*
 *  tests/exceptions_threads.cpp
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

#include "../mutex.h"

#include "tests.h"

using namespace std;

volatile bool stopFlag = false;
BareMutex okMutex(BareMutex::NoLogging);
int ok = 0;

void* threadFn(void* arg) throw () {
    volatile int* counter = static_cast<volatile int*>(arg);
    while (!stopFlag) {
        try {
            if (!stopFlag) // make sure the throw isn't optimized away
                throw 0;
        } catch (int) {
            ++*counter;
        }
    }
    Lock l(okMutex);
    ++ok;
    return 0;
}

void threadedExceptionTest() throw () {
    SimpleThread thread1, thread2;
    volatile int ex0 = 0, ex1 = 0, ex2 = 0;
    thread1.start(threadFn, const_cast<int*>(&ex1));
    thread2.start(threadFn, const_cast<int*>(&ex2));
    while (ex0 < 10000 || ex1 < 10000 || ex2 < 10000) {
        try {
            if (!stopFlag)
                throw 0;
        } catch (int) {
            ++ex0;
        }
    }
    stopFlag = true;
    thread1.join();
    thread2.join();
    nAssert(ok == 2);
}

int main() {
    threadedExceptionTest();
    return 0;
}
