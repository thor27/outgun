/*
 *  log.cpp
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

#include "platform.h"
#include "timer.h"

#include "log.h"

using std::string;

Log::Log() throw () :
    m(Mutex::NoLogging),
    nLines(0)
{ }

void Log::put(const string& str) throw () {
    Lock l(*this);
    add(str);
    ++nLines;
}

void Log::operator()(const char* fmt, ...) throw () {
    va_list args;
    va_start(args, fmt);
    operator()(fmt, args);
    va_end(args);
}

void Log::operator()(const char* fmt, va_list args) throw () {
    char buf[4000];
    platVsnprintf(buf, 4000, fmt, args);
    put(buf);
}

int Log::numLines() const throw () {
    return nLines;
}

FileLog::FileLog(const string& filename, bool truncate) throw () {
    fileName = filename;
    printDate = !truncate;
    fp = fopen(fileName.c_str(), (truncate ? "wt" : "at"));
}

FileLog::~FileLog() throw () {
    if (!fp)
        return;
    const bool emptyFile = (ftell(fp) == 0);
    fclose(fp);
    if (emptyFile)
        remove(fileName.c_str());
}

void FileLog::add(const string& str) throw () {
    if (!fp)
        return;
    if (printDate) {
        fputs(date_and_time().c_str(), fp);
        fputs("  ", fp);
    }
    else {
        g_timeCounter.refresh(); // just to be accurate
        fprintf(fp, "%9.2f: ", get_time());
    }
    fprintf(fp, "%s\n", str.c_str());
    fflush(fp);
}

void MemoryLog::add(const string& str) throw () {
    data.push_back(str);
}

string MemoryLog::pop() throw () {
    Lock l(*this);
    if (data.empty())
        return string();
    const string s = data.front();
    data.pop_front();
    return s;
}

void FileMemLog::add(const string& str) throw () {
    FileLog::add(str);
    MemoryLog::add(str);
}

void DualLog::add(const string& str) throw () {
    log1.put(prefix1 + str);
    log2.put(prefix2 + str);
}
