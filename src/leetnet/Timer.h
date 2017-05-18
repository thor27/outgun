#ifndef TIMER_H_INCLUDED_C517B9FE
#define TIMER_H_INCLUDED_C517B9FE

/* GNE - Game Networking Engine, a portable multithreaded networking library.
 * Copyright (C) 2001 Jason Winnebeck (gillius@mail.rit.edu)
 * This file modified (unneeded features stripped) by Niko Ritari 2004
 * Project website: http://www.rit.edu/~jpw9607/
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "gneintern.h"
#include "Time.h"

namespace GNE {

/**
 * The timer class is used to get the current time and to provide callbacks.
 * A timer object calls its listeners back every so often based on the time
 * given.
 *
 * All of the methods in this class are safe to call from multiple threads at
 * the same time, and can also be called from the TimerCallback as well, with
 * a few (some obvious) exceptions.
 */
//##ModelId=3B075380037B
class Timer {
public:
  /**
   * Returns the current time from some arbitray point in the past.  This is
   * usually a very high precision timer that likely provides microsecond
   * or better resolution.
   */
  //##ModelId=3B0753820036
  static Time getCurrentTime();

  /**
   * Returns the current time from the system clock.
   *
   * The time returned is an absolute time, usually relative to midnight,
   * Jan 1, 1970.
   */
  //##ModelId=3B0753820065
  static Time getAbsoluteTime();
};

}
#endif /* TIMER_H_INCLUDED_C517B9FE */
