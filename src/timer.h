/*
 *  timer.h
 *
 *  Copyright (C) 2006 - Niko Ritari
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

#ifndef TIMER_H_INC
#define TIMER_H_INC

class SystemTimer {
public:
    virtual ~SystemTimer() { }
    virtual double read() = 0;
};

extern SystemTimer* g_systemTimer; // defined in globals.cpp; created in platInit()

class TimeCounter { // usable only when g_systemTimer is, that is after platInit()
    double value, base;

public:
    TimeCounter() : value(0), base(0) { }
    void setZero() { base = g_systemTimer->read(); value = 0; }
    void refresh() { value = g_systemTimer->read() - base; }
    double read() const { return value; }
};

extern TimeCounter g_timeCounter; // defined in globals.cpp

inline double get_time() { return g_timeCounter.read(); }

// don't use platSleep(0) in order to accomplish anything; sched_yield() works on every platform while platSleep(0) doesn't
void platSleep(unsigned ms); // defined in platform*.cpp

#endif
