/*
 *  debug.h
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

#ifndef DEBUG_H_INC
#define DEBUG_H_INC

class Log;
class LogSet;

// thread ID logging; this is controlled by LOG_THREAD_IDS in debugconfig.h
void logThreadEvent(bool exit, const char* function, Log& log);
void logThreadEvent(bool exit, const char* function, LogSet& log);  // simply dumps to the normal log of the LogSet
inline void logThreadStart(const char* function, Log& log) { logThreadEvent(false, function, log); }
inline void logThreadExit (const char* function, Log& log) { logThreadEvent(true , function, log); }
inline void logThreadStart(const char* function, LogSet& log) { logThreadEvent(false, function, log); }
inline void logThreadExit (const char* function, LogSet& log) { logThreadEvent(true , function, log); }

#ifdef NDEBUG

class ValidityChecker {
public:
    void checkValidity() const { }
};

typedef ValidityChecker PointerLeakBuffer;

#else   // NDEBUG

#include <iostream>
#include <stdio.h>
#include "commont.h"
#include "nassert.h"

class ValidityChecker {
    int check;

  public:
    ValidityChecker() : check(0xC044EC7) { }
    void checkValidity() const
        { numAssert((unsigned)this>0x10000 && (unsigned)this<0xFFFF0000, (int)this); nAssert(check!=0xDE7E7ED); nAssert(check==0xC044EC7); }
    ~ValidityChecker() { checkValidity(); check=0xDE7E7ED; }
};

#pragma pack(push, 1)
template<int size> class PointerLeakBuffer : private ValidityChecker {
    unsigned char buffer[size];

  public:
    PointerLeakBuffer() {
        for (int i=0; i<size; i++)
            buffer[i]=0x11;
    }
    void checkValidity() const {
        for (int i=0; i<size; i++)
            if (buffer[i]!=0x11) {
                printf("Leak buffer (@%p-%p) changed at %d (%p), data: ", &buffer[0], &buffer[size-1], i, &buffer[i]);
                for (int x=i; x<size && buffer[x]!=0x11; x++)
                    printf("%02X ", buffer[x]);
                printf("\nas string: ");
                for (; i<size && buffer[i]!=0x11; i++)
                    printf("%c", buffer[i]);
                printf("\n\n");
            }
        ValidityChecker::checkValidity();
    }
    ~PointerLeakBuffer() {
        checkValidity();
    }
};
#pragma pack(pop)

#endif  // NDEBUG

#endif  // DEBUG_H_INC
