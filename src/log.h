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

#include <deque>
#include <string>

#include <cstdarg>
#include <cstdio>

#include "mutex.h"
#include "network.h"
#include "utility.h"

class Log : public NoCopying, protected ConstLockable { // base class
    mutable Mutex m;
    int nLines;

    virtual void add(const std::string& str) throw () = 0;

protected:
    // note: operator()() and put() lock the mutex automatically, so it is already locked on an add() call
    void lock() const throw () { m.lock(); }
    void unlock() const throw () { m.unlock(); }

public:
    Log() throw ();
    virtual ~Log() throw () { }
    void put(const std::string& str) throw ();
    void operator()(const char* fmt, ...) throw () PRINTF_FORMAT(2, 3);
    void operator()(const char* fmt, va_list args) throw () PRINTF_FORMAT(2, 0);
    int numLines() const throw ();
};

class NoLog : public Log {
    void add(const std::string&) throw () { }

public:
    NoLog() throw () { }
};

class FileLog : public virtual Log {
    FILE* fp;
    std::string fileName;
    bool printDate;

protected:
    virtual void add(const std::string& str) throw ();

public:
    FileLog(const std::string& filename, bool truncate) throw ();
    virtual ~FileLog() throw ();
};

class MemoryLog : public virtual Log {
    std::deque<std::string> data;

protected:
    virtual void add(const std::string& str) throw ();

public:
    MemoryLog() throw () { }
    virtual ~MemoryLog() throw () { }
    int size() const throw () { ConstLock l(*this); return data.size(); }
    std::string pop() throw ();  // returns empty string when there's nothing more to pop
};

class FileMemLog : public FileLog, public MemoryLog {
protected:
    virtual void add(const std::string& str) throw ();

public:
    FileMemLog(const std::string& filename, bool truncate) throw () : FileLog(filename, truncate) { }
};

class DualLog : public virtual Log {    // the most flexible way of multiplying output
    Log& log1, & log2;
    std::string prefix1, prefix2;

protected:
    virtual void add(const std::string& str) throw ();

public:
    DualLog(Log& hostLog1, Log& hostLog2, const std::string outputPrefix1 = std::string(), const std::string outputPrefix2 = std::string()) throw ()
        : log1(hostLog1), log2(hostLog2), prefix1(outputPrefix1), prefix2(outputPrefix2) { }
    virtual ~DualLog() throw () { }
};

template<class Base>    // Base can be any Log variant
class SupplementaryLog : public Base {
    Log& host;
    std::string prefix;

protected:
    virtual void add(const std::string& str) throw () {
        host.put(prefix + str);
        Base::add(str);
    }

public:
    SupplementaryLog(Log& hostLog, const std::string& outputPrefix) throw () : host(hostLog), prefix(outputPrefix) { }
    ~SupplementaryLog() throw () { }
    template<class A1> SupplementaryLog(Log& hostLog, const std::string& outputPrefix, A1 a1) throw () : Base(a1), host(hostLog), prefix(outputPrefix) { }
    template<class A1, class A2> SupplementaryLog(Log& hostLog, const std::string& outputPrefix, A1 a1, A2 a2) throw () : Base(a1, a2), host(hostLog), prefix(outputPrefix) { }
};

#endif
