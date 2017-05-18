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
#include "ConditionVariable.h"
#include "Mutex.h"
#include "Time.h"
#include "Timer.h"

namespace GNE {

//##ModelId=3B07538003CD
ConditionVariable::ConditionVariable() {
  valassert(pthread_cond_init( &cond, NULL ), 0);
  ourMutex = true;
  mutex = new Mutex();
}

ConditionVariable::ConditionVariable(Mutex* mutex_) {
  valassert(pthread_cond_init( &cond, NULL ), 0);
  ourMutex = false;
  mutex = mutex_;
}

//##ModelId=3B07538003D0
ConditionVariable::~ConditionVariable() {
  valassert(pthread_cond_destroy( &cond ), 0);
  if (ourMutex)
    delete mutex;
}

//##ModelId=3B0753810002
void ConditionVariable::wait() {
  valassert(pthread_cond_wait(&cond, &mutex->mutex), 0);
}

//##ModelId=3B0753810003
void ConditionVariable::timedWait(int ms) {
  Time t = Timer::getAbsoluteTime();
  t += ms*1000;
  timespec tv;
  tv.tv_sec = t.getSec();
  tv.tv_nsec = t.getuSec() * 1000;
  pthread_cond_timedwait(&cond, &(mutex->mutex), &tv);
}

//##ModelId=3B0753810005
void ConditionVariable::signal() {
  valassert(pthread_cond_signal( &cond ), 0);
}

}
