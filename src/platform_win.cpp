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

#include <cstdio>
#include <cstring>

#include <direct.h>
#include <sys/types.h>

#include "commont.h"
#include "platform.h"
#include "timer.h"
#include "incalleg.h"

#include "mutex.h"

using std::string;

int platStricmp(const char* s1, const char* s2) throw () {
    return stricmp(s1, s2);
}

int platVsnprintf(char* buf, size_t count, const char* fmt, va_list arg) throw () {
    return _vsnprintf(buf, count, fmt, arg);
}

#ifndef DEDICATED_SERVER_ONLY
void platMessageBox(const string& caption, const string& msg, bool blocking) throw () {
    (void)blocking; // can't produce nonblocking messages too easily
    MessageBox(NULL, msg.c_str(), caption.c_str(), MB_OK);
}
#endif

class PerformanceCounterTimer : public SystemTimer {
    double mul;

public:
    bool init() throw () { // returns false if performance counters are not available
        LARGE_INTEGER freq;
        if (!QueryPerformanceFrequency(&freq))
            return false;
        mul = 1. / double(freq.QuadPart);
        return true;
    }

    double read() throw () {
        LARGE_INTEGER value;
        if (!QueryPerformanceCounter(&value))
            nAssert(0);
        return double(value.QuadPart) * mul;
    }
};

class MMSystemTimer : public SystemTimer {
    uint64_t base;
    uint32_t prev;

    Mutex readMutex; // read needs to be locked to avoid extra additions to base

public:
    MMSystemTimer() throw () : readMutex(Mutex::NoLogging) {
        base = 0;
        prev = static_cast<uint32_t>(timeGetTime());
    }

    double read() throw () {
        Lock ml(readMutex);
        uint32_t val = static_cast<uint32_t>(timeGetTime());
        if (val < prev) // check wrap-around
            base += uint64_t(1) << 32;
        prev = val;
        return double(base + val) * .001; // value from timeGetTime is in ms
    }
};

void platSleep(unsigned ms) throw () {
    Sleep(ms);
}

#ifndef DEDICATED_SERVER_ONLY
class AllegroFileFinder : public FileFinder {
    bool directories;
    al_ffblk ffblk;
    int findResult;

    void skipUntilValid() throw () {
        if (!directories)
            return;
        while (!(ffblk.attrib & FA_DIREC) || !strcmp(ffblk.name, ".") || !strcmp(ffblk.name, "..")) {
            findResult = al_findnext(&ffblk);
            if (findResult != 0)
                return;
        }
    }

public:
    AllegroFileFinder(const string& path, const string& extension, bool directories_) throw () : directories(directories_) {
        int attrib = FA_ARCH | FA_RDONLY;
        if (directories)
            attrib |= FA_DIREC;
        findResult = al_findfirst((path + directory_separator + '*' + extension).c_str(), &ffblk, attrib);
        skipUntilValid();
    }

    ~AllegroFileFinder() throw () {
        al_findclose(&ffblk);
    }

    bool hasNext() const throw () { return findResult == 0; }

    string next() throw () {
        string name = ffblk.name;
        findResult = al_findnext(&ffblk);
        skipUntilValid();
        return name;
    }
};

ControlledPtr<FileFinder> platMakeFileFinder(const string& path, const string& extension, bool directories) throw () {
    return give_control(new AllegroFileFinder(path, extension, directories));
}

bool platIsFile(const string& name) throw () {
    return exists(name.c_str());
}

bool platIsDirectory(const string& name) throw () {
    int attr;
    if (!file_exists(name.c_str(), FA_DIREC | FA_ARCH | FA_RDONLY, &attr))
        return false;
    return (attr & FA_DIREC) != 0;
}

int platMkdir(const string& path) throw () {
    return mkdir(path.c_str());
}
#endif // DEDICATED_SERVER_ONLY

static UINT g_timerResolution;

void platInit() throw () {
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

void platInitAfterAllegro() throw () {
    #ifndef DEDICATED_SERVER_ONLY
    static const int bufSize = 1000;
    char pathBuf[bufSize];
    get_executable_name(pathBuf, bufSize);
    replace_filename(pathBuf, pathBuf, "", bufSize);
    wheregamedir = pathBuf;
    #else
    wheregamedir = "./";
    #endif
}

void platUninit() throw () {
    delete g_systemTimer;
    g_systemTimer = 0;
    timeEndPeriod(g_timerResolution);
}
