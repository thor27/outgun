/*
 *  platform_win.cpp
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

#include <direct.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <sys/types.h>

#include "commont.h"
#include "incalleg.h"
#include "mutex.h"
#include "platform.h"
#include "timer.h"

using std::string;

int platStricmp(const char* s1, const char* s2) {
    return stricmp(s1, s2);
}

int platVsnprintf(char* buf, size_t count, const char* fmt, va_list arg) {
    return _vsnprintf(buf, count, fmt, arg);
}

void platMessageBox(const string& caption, const string& msg, bool blocking) {
    (void)blocking; // can't produce nonblocking messages too easily
    MessageBox(NULL, msg.c_str(), caption.c_str(), MB_OK);
}

class PerformanceCounterTimer : public SystemTimer {
    double mul;

public:
    bool init() { // returns false if performance counters are not available
        LARGE_INTEGER freq;
        if (!QueryPerformanceFrequency(&freq))
            return false;
        mul = 1. / double(freq.QuadPart);
        return true;
    }

    double read() {
        LARGE_INTEGER value;
        if (!QueryPerformanceCounter(&value))
            nAssert(0);
        return double(value.QuadPart) * mul;
    }
};

class MMSystemTimer : public SystemTimer {
    uint64_t base;
    uint32_t prev;

    MutexHolder readMutex; // read needs to be locked to avoid extra additions to base

public:
    MMSystemTimer() {
        base = 0;
        prev = static_cast<uint32_t>(timeGetTime());
    }

    double read() {
        MutexLock ml(readMutex);
        uint32_t val = static_cast<uint32_t>(timeGetTime());
        if (val < prev) // check wrap-around
            base += uint64_t(1) << 32;
        prev = val;
        return double(base + val) * .001; // value from timeGetTime is in ms
    }
};

void platSleep(unsigned ms) {
    Sleep(ms);
}

class AllegroFileFinder : public FileFinder {
    bool directories;
    al_ffblk ffblk;
    int findResult;

    void skipUntilValid() {
        if (!directories)
            return;
        while (!(ffblk.attrib & FA_DIREC) || !strcmp(ffblk.name, ".") || !strcmp(ffblk.name, "..")) {
            findResult = al_findnext(&ffblk);
            if (findResult != 0)
                return;
        }
    }

public:
    AllegroFileFinder(const string& path, const string& extension, bool directories_) : directories(directories_) {
        int attrib = FA_ARCH | FA_RDONLY;
        if (directories)
            attrib |= FA_DIREC;
        findResult = al_findfirst((path + directory_separator + '*' + extension).c_str(), &ffblk, attrib);
        skipUntilValid();
    }

    ~AllegroFileFinder() {
        al_findclose(&ffblk);
    }

    bool hasNext() const { return findResult == 0; }

    string next() {
        string name = ffblk.name;
        findResult = al_findnext(&ffblk);
        skipUntilValid();
        return name;
    }
};

FileFinder* platMakeFileFinder(const string& path, const string& extension, bool directories) {
    return new AllegroFileFinder(path, extension, directories);
}

bool platIsFile(const string& name) {
    return exists(name.c_str());
}

bool platIsDirectory(const string& name) {
    int attr;
    if (!file_exists(name.c_str(), FA_DIREC | FA_ARCH | FA_RDONLY, &attr))
        return false;
    return (attr & FA_DIREC) != 0;
}

int platMkdir(const string& path) {
    return mkdir(path.c_str());
}

static UINT g_timerResolution;

void platInit() {
    directory_separator = '\\';

    // increase resolution of timers for the benefit of both Sleep (used by platSleep) and timeGetTime (used by MMSystemTimer)
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR)
        g_timerResolution = 1; // try something anyway
    else
        g_timerResolution = tc.wPeriodMin;
    timeBeginPeriod(g_timerResolution); // ignore possible failure; we can't help it

    PerformanceCounterTimer* t = new PerformanceCounterTimer();
    if (t->init())
        g_systemTimer = t;
    else  {
        delete t;
        g_systemTimer = new MMSystemTimer();
    }
}

void platUninit() {
    delete g_systemTimer;
    g_systemTimer = 0;
    timeEndPeriod(g_timerResolution);
}
