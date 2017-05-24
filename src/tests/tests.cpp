/*
 *  tests.cpp
 *
 *  Copyright (C) 2008 - Niko Ritari
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

#include <iostream>

#include "tests.h"
#include "../nassert.h"

using namespace std;

// replace contents of nassert.cpp in a way that supports testing assertions (this means that tests can't be linked with nassert.o)

bool expectAssertion = false;

void nAssertFail(const char* expr, const char* file, int line) throw () {
    if (expectAssertion)
        pthread_exit(0);
    cerr << "Assertion failed: " << expr << " in " << file << ", line " << line << '\n';
    abort();
}

#define ARGP const char*, int
void nAssertFail(const char* expr, ARGP, const char* file, int line) throw () { nAssertFail(expr, file, line); }
void nAssertFail(const char* expr, ARGP, ARGP, const char* file, int line) throw () { nAssertFail(expr, file, line); }
void nAssertFail(const char* expr, ARGP, ARGP, ARGP, const char* file, int line) throw () { nAssertFail(expr, file, line); }
void nAssertFail(const char* expr, ARGP, ARGP, ARGP, ARGP, const char* file, int line) throw () { nAssertFail(expr, file, line); }
#undef ARGP

uint32_t* stackGuardHackPtr;
