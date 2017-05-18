/*
 *  log.h
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

#ifndef LOG_H_INC
#define LOG_H_INC

#include <cstdio>
#include <string>
#include <deque>
#include <cstdarg>
#include <nl.h>
#include "mutex.h"
#include "utility.h"

class Log { // base class
    mutable MutexHolder m;
    int nLines;

    virtual void add(const std::string& str) =0;

    Log(const Log&);    // log objects aren't supposed to be copied; these aren't implemented anywhere to ensure that
    Log& operator=(const Log&);

protected:
    // note: operator()() and put() lock the mutex automatically, so it is already locked on an add() call
    void lock() const { m.lock(); }
    void unlock() const { m.unlock(); }

public:
    Log() : nLines(0) { }
    virtual ~Log() { }
    void put(const std::string& str);
    void operator()(const char* fmt, ...) PRINTF_FORMAT(2, 3);
    void operator()(const char* fmt, va_list args) PRINTF_FORMAT(2, 0);
    int numLines() const;
};

class NoLog : public Log {
    void add(const std::string&) { }

public:
    NoLog() { }
};

class FileLog : public virtual Log {
    FILE* fp;
    std::string fileName;
    bool printDate;

protected:
    virtual void add(const std::string& str);

public:
    FileLog(const std::string& filename, bool truncate);
    virtual ~FileLog();
};

class MemoryLog : public virtual Log {
    std::deque<std::string> data;

protected:
    virtual void add(const std::string& str);

public:
    MemoryLog() { }
    virtual ~MemoryLog() { }
    int size() const { lock(); int sz = data.size(); unlock(); return sz; }
    std::string pop();  // returns empty string when there's nothing more to pop
};

class FileMemLog : public FileLog, public MemoryLog {
protected:
    virtual void add(const std::string& str);

public:
    FileMemLog(const std::string& filename, bool truncate) : FileLog(filename, truncate) { }
};

class DualLog : public virtual Log {    // the most flexible way of multiplying output
    Log& log1, & log2;
    std::string prefix1, prefix2;

protected:
    virtual void add(const std::string& str);

public:
    DualLog(Log& hostLog1, Log& hostLog2, const std::string outputPrefix1 = std::string(), const std::string outputPrefix2 = std::string())
        : log1(hostLog1), log2(hostLog2), prefix1(outputPrefix1), prefix2(outputPrefix2) { }
    virtual ~DualLog() { }
};

template<class Base>    // Base can be any Log variant
class SupplementaryLog : public Base {
    Log& host;
    std::string prefix;

protected:
    virtual void add(const std::string& str) {
        host.put(prefix + str);
        Base::add(str);
    }

public:
    SupplementaryLog(Log& hostLog, const std::string& outputPrefix) : host(hostLog), prefix(outputPrefix) { }
    template<class A1> SupplementaryLog(Log& hostLog, const std::string& outputPrefix, A1 a1) : Base(a1), host(hostLog), prefix(outputPrefix) { }
    template<class A1, class A2> SupplementaryLog(Log& hostLog, const std::string& outputPrefix, A1 a1, A2 a2) : Base(a1, a2), host(hostLog), prefix(outputPrefix) { }
};

#endif
