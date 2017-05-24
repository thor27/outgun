/*
 *  debug.h
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

#ifndef DEBUG_H_INC
#define DEBUG_H_INC

#include <cstring>
#include <cstdio>
#include "mutex.h"

class ThreadLog {
    FILE* file;

    void beginEntry() throw ();
    void endEntry() throw ();

    typedef uint32_t ThreadId;
    typedef uint64_t ObjectId;
    static ThreadId idThread(pthread_t t) throw () {
        ThreadId id = 0;
        memcpy(&id, &t, std::min(sizeof(ThreadId), sizeof(pthread_t))); // since we have no idea what type pthread_t actually is on each system
        return id;
    }
    static ObjectId idObject(void* p)     throw () { return static_cast<ObjectId>(reinterpret_cast<intptr_t>(p)); }

    friend class ThreadLogWriter;

public:
    ThreadLog() throw () : file(0) { }
    ~ThreadLog() throw () { if (file) fclose(file); }
};

class ThreadLogWriter : private NoCopying {
    ThreadLog& host;

public:
    ThreadLogWriter(ThreadLog& host_) throw () : host(host_) {
        host.beginEntry();
        putThreadId(pthread_self());
    }
    ~ThreadLogWriter() throw () { host.endEntry(); }

    template<class Type> void put(Type t) throw () { fwrite(&t, sizeof(t), 1, host.file); }
    void put(const char* str) throw () { fputs(str, host.file); fputc('\0', host.file); }
    void putThreadId(pthread_t t) throw () { put(ThreadLog::idThread(t)); }
    void putObjectId(void* p) throw () { put(ThreadLog::idObject(p)); }
};

extern ThreadLog g_threadLog;
extern BareMutex g_threadLogMutex;

#ifdef NDEBUG

class ValidityChecker {
public:
    void checkValidity() const throw () { }
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
    ValidityChecker() throw () : check(0xC044EC7) { }
    void checkValidity() const throw () { nAssert(this != 0); nAssert(check!=0xDE7E7ED); nAssert(check==0xC044EC7); }
    ~ValidityChecker() throw () { checkValidity(); check=0xDE7E7ED; }
};

#pragma pack(push, 1)
template<int size> class PointerLeakBuffer : private ValidityChecker {
    unsigned char buffer[size];

  public:
    PointerLeakBuffer() throw () {
        for (int i=0; i<size; i++)
            buffer[i]=0x11;
    }
    void checkValidity() const throw () {
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
    ~PointerLeakBuffer() throw () {
        checkValidity();
    }
};
#pragma pack(pop)

#endif  // NDEBUG

#endif  // DEBUG_H_INC
