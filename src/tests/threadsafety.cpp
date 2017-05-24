/*
 *  tests/threadsafety.cpp
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
BareMutex mutex(BareMutex::NoLogging);
bool someoneUsing = false;
volatile int bogus;

void* threadFn(void* arg) throw () {
    volatile int* counter = static_cast<volatile int*>(arg);
    while (!stopFlag) {
        mutex.lock();
        nAssert(!someoneUsing);
        someoneUsing = true;
        for (int i = 0; i < 1000; ++i)
            ++bogus;
        ++*counter;
        someoneUsing = false;
        mutex.unlock();
        for (int i = 0; i < 500; ++i)
            ++bogus;
    }
    return 0;
}

void basicThreadSafetyTest() throw () {
    SimpleThread thread1, thread2;
    volatile int ex0 = 0, ex1 = 0, ex2 = 0;
    thread1.start(threadFn, const_cast<int*>(&ex1));
    thread2.start(threadFn, const_cast<int*>(&ex2));
    while (ex0 < 10000 || ex1 < 10000 || ex2 < 10000) {
        mutex.lock();
        nAssert(!someoneUsing);
        someoneUsing = true;
        for (int i = 0; i < 1000; ++i)
            ++bogus;
        ++ex0;
        someoneUsing = false;
        mutex.unlock();
        for (int i = 0; i < 500; ++i)
            ++bogus;
    }
    stopFlag = true;
    thread1.join();
    thread2.join();
}

int main() {
    basicThreadSafetyTest();
    return 0;
}
