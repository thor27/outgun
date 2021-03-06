/* GNE - Game Networking Engine, a portable multithreaded networking library.
 * Copyright (C) 2001 Jason Winnebeck (gillius@mail.rit.edu)
 * This file modified (unneeded features stripped) by Niko Ritari 2003, 2004
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
#include "Timer.h"
#include "Time.h"

namespace GNE {

#ifndef WIN32
//For the gettimeofday function.
#include <sys/time.h>
#endif

//##ModelId=3B0753820036
Time Timer::getCurrentTime() throw () {
  Time ret;
#ifdef WIN32
  LARGE_INTEGER t, freq;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t);
  ret.setSec(int(t.QuadPart / freq.QuadPart));
  ret.setuSec(int((t.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart));
#else
  timeval tv;
  gettimeofday(&tv, NULL);
  ret.setSec(tv.tv_sec);
  ret.setuSec(tv.tv_usec);
#endif
  return ret;
}

//##ModelId=3B0753820065
Time Timer::getAbsoluteTime() throw () {
#ifdef WIN32
  Time ret;
  _timeb t;
  _ftime(&t);
  ret.setSec(t.time);
  ret.setuSec(t.millitm * 1000);
  return ret;
#else
  return getCurrentTime();
#endif
}
}
