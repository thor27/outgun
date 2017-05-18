/* GNE - Game Networking Engine, a portable multithreaded networking library.
 * Copyright (C) 2001 Jason Winnebeck (gillius@mail.rit.edu)
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
#include "Mutex.h"

#include <pthread.h>

namespace GNE {

//##ModelId=3B075381014B
Mutex::Mutex() {
  pthread_mutexattr_t attr;
  valassert(pthread_mutexattr_init(&attr), 0);
#ifdef LINUX
//  valassert(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP), 0);
#else
//  valassert(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE), 0);
#endif
  valassert(pthread_mutex_init( &mutex, &attr ), 0);
  valassert(pthread_mutexattr_destroy(&attr), 0);
}

//##ModelId=3B075381014C
Mutex::~Mutex() {
  valassert(pthread_mutex_destroy( &mutex ), 0);
}

//##ModelId=3B075381014E
void Mutex::acquire() {
  valassert(pthread_mutex_lock( &mutex ), 0);
}

//##ModelId=3B075381014F
void Mutex::release() {
  valassert(pthread_mutex_unlock( &mutex ), 0);
}

}





