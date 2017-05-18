/*
 *  nassert.cpp
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
#include <cstdarg>
#include <cstdlib>  // abort
#include <ctime>
#include "incalleg.h"

#ifndef DISABLE_ENHANCED_NASSERT
#include <nl.h>
#include "commont.h"
#include "debugconfig.h"
#include "language.h"
#include "mutex.h"
#include "platform.h"
#include "utility.h"
#endif

#include "nassert.h"

#ifndef DISABLE_ENHANCED_NASSERT
unsigned long* stackGuardHackPtr;

void stackDump(FILE* dst) { // makes heavy assumptions about processor architecture wrt stack! Should work fine on any x86 platform.
    unsigned long unused;
    for (unsigned long* stackPtr = (&unused) + 1; *stackPtr != STACK_GUARD; ++stackPtr) {
        unsigned long value = *stackPtr;
        fwrite(&stackPtr, sizeof(stackPtr), 1, dst);
        fwrite(&value, sizeof(value), 1, dst);
    }
}

int stackDump(char* buf, int bufCap) {  // returns the size used; max bufCap
    unsigned long unused;
    bufCap -= 4;    // make it better work as a stopper
    int bufSize = 0;
    unsigned long* stackPtr = (&unused) + 1;
    writeLong(buf, bufSize, reinterpret_cast<unsigned long>(stackPtr));
    for (; *stackPtr != STACK_GUARD && bufSize <= bufCap; ++stackPtr)
        writeLong(buf, bufSize, *stackPtr);
    return bufSize;
}

void nasprintf(const char* file, int line, const char* expr, ...) PRINTF_FORMAT(3, 4); // PRINTF_FORMAT is defined in utility.h
#endif

void nasprintf(const char* file, int line, const char* expr, ...) {
    // display to console (should be safest, but rarely visible on Windows)
    fprintf(stderr, "Assertion failed: ");
    va_list argptr;
    va_start(argptr, expr);
    vfprintf(stderr, expr, argptr);
    va_end(argptr);
    fprintf(stderr, ", file %s, line %d\n", file, line);
    #ifndef DISABLE_ENHANCED_NASSERT
    // save to assert.log
    FILE* asfile = fopen((wheregamedir + "log" + directory_separator + "assert.log").c_str(), "at");
    if (asfile) {
        fprintf(asfile, "%s  %s  %s:%d ", date_and_time().c_str(), GAME_SHORT_VERSION, file, line);
        va_start(argptr, expr);
        vfprintf(asfile, expr, argptr);
        va_end(argptr);
        fprintf(asfile, "\n");
        fclose(asfile);
        FILE* stdump = fopen((wheregamedir + "log" + directory_separator + "stackdump.bin").c_str(), "wb");
        if (stdump) {
            stackDump(stdump);
            fclose(stdump);
        }
    }
    // display using messageBox
    va_start(argptr, expr);
    char* buf = new char[10000];    // must allocate from heap, otherwise this fills the stack dump; no need to free as we're already exiting
    platVsnprintf(buf, 10000, expr, argptr);
    va_end(argptr);
    messageBox(_("Internal error"), std::string("Assertion failed: ") + std::string(buf) + ", file " + file + ", line " + itoa(line) + '\n' +
               _("This results from a bug in Outgun. To help us fix it, please send assert.log and stackdump.bin from the log directory and describe what you were doing to outgun@mbnet.fi"));
    // finally, throw it to the net and hope it's caught
    if (g_autoBugReporting == ABR_disabled || strcmp(GAME_BRANCH, "base"))
        return;
    static const int stackDumpSize = 10000; // it splits into 7 Ethernet frames and so has a good chance of getting lost, but much less size isn't useful
    char* mbuf = new char[stackDumpSize];   // must allocate from heap, otherwise this fills the stack dump; no need to free as we're already exiting
    int len = 0;
    writeString(mbuf, len, GAME_STRING);
    writeString(mbuf, len, GAME_SHORT_VERSION);
    #ifdef OFFICIAL_BUILD_ID
    // please don't define OFFICIAL_BUILD_ID unless you've set up your own bug report server, configured it in MasterSettings::load, and agreed about it with whoever is keeping the server list master server your version uses so that the master won't override the setting in what is downloaded to master.txt
    writeByte(mbuf, len, OFFICIAL_BUILD_ID);
    #else
    writeByte(mbuf, len, 0);
    #endif
    writeString(mbuf, len, file);
    writeLong(mbuf, len, line);
    if (g_autoBugReporting == ABR_withDump) {
        buf[200] = '\0';    // truncate extremely long messages
        writeString(mbuf, len, buf);
    }
    nlOpenMutex.lock();
    nlDisable(NL_BLOCKING_IO);
    NLsocket dbgSock = nlOpen(0, NL_UNRELIABLE);
    nlOpenMutex.unlock();
    if (dbgSock == NL_INVALID)
        return;
    nlSetRemoteAddr(dbgSock, &g_masterSettings.bugReportAddress());
    nlWrite(dbgSock, mbuf, len);
    #ifdef OFFICIAL_BUILD_ID
    // see note about OFFICIAL_BUILD_ID above
    // the stack dumps are only useful from known executables
    if (g_autoBugReporting == ABR_withDump) {
        // generate a secondary packet with the top of stack dump information
        len = stackDump(mbuf, stackDumpSize);
        nlWrite(dbgSock, mbuf, len);
    }
    #endif
    nlClose(dbgSock);
    #endif // DISABLE_ENHANCED_NASSERT
}

#define ARGP(num) const char* name##num, int val##num

void nAssertFail(const char* expr, const char* file, int line) {
    nasprintf(file, line, "%s",
              expr);
    abort();
}

void nAssertFail(const char* expr, ARGP(1), const char* file, int line) {
    nasprintf(file, line, "%s (%s==%d)",
              expr, name1, val1);
    abort();
}

void nAssertFail(const char* expr, ARGP(1), ARGP(2), const char* file, int line) {
    nasprintf(file, line, "%s (%s==%d, %s==%d)",
              expr, name1, val1, name2, val2);
    abort();
}

void nAssertFail(const char* expr, ARGP(1), ARGP(2), ARGP(3), const char* file, int line) {
    nasprintf(file, line, "%s (%s==%d, %s==%d, %s==%d)",
              expr, name1, val1, name2, val2, name3, val3);
    abort();
}

void nAssertFail(const char* expr, ARGP(1), ARGP(2), ARGP(3), ARGP(4), const char* file, int line) {
    nasprintf(file, line, "%s (%s==%d, %s==%d, %s==%d, %s==%d)",
              expr, name1, val1, name2, val2, name3, val3, name4, val4);
    abort();
}

#undef ARGP
