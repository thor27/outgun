/*
 *  nassert.h
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

#ifndef NASSERT_H_INC
#define NASSERT_H_INC

#ifndef __GNUC__
#define __attribute__(a)
#endif

// stack guard is a magic number that should be somewhere on the stack to prevent the stack dump from going further and making a page fault
// you should have a local variable unsigned long stackGuard = STACK_GUARD; in main() and all thread entry functions
static const unsigned long STACK_GUARD = 0x39D1209E;
extern unsigned long* stackGuardHackPtr;    // set stackGuardHackPtr = &stackGuard to make sure the stackGuard variable isn't optimized away

#ifdef NDEBUG
 #define nAssert(expr) ((void)0)
 #define numAssert(expr, v1) ((void)0)
 #define numAssert2(expr, v1, v2) ((void)0)
 #define numAssert3(expr, v1, v2, v3) ((void)0)
 #define numAssert4(expr, v1, v2, v3, v4) ((void)0)
#else // NDEBUG
 #define ARGP const char*, int
 void nAssertFail(const char* expr, const char* file, int line) __attribute__ ((noreturn));
 void nAssertFail(const char* expr, ARGP, const char* file, int line) __attribute__ ((noreturn));
 void nAssertFail(const char* expr, ARGP, ARGP, const char* file, int line) __attribute__ ((noreturn));
 void nAssertFail(const char* expr, ARGP, ARGP, ARGP, const char* file, int line) __attribute__ ((noreturn));
void nAssertFail(const char* expr, ARGP, ARGP, ARGP, ARGP, const char* file, int line) __attribute__ ((noreturn));
 #undef ARGP
 #define nAssert(expr)                      ((expr)?(void)0:nAssertFail(#expr, __FILE__, __LINE__))
 #define numAssert(expr, v1)                ((expr)?(void)0:nAssertFail(#expr, #v1, v1, __FILE__, __LINE__))
 #define numAssert2(expr, v1, v2)           ((expr)?(void)0:nAssertFail(#expr, #v1, v1, #v2, v2, __FILE__, __LINE__))
 #define numAssert3(expr, v1, v2, v3)       ((expr)?(void)0:nAssertFail(#expr, #v1, v1, #v2, v2, #v3, v3, __FILE__, __LINE__))
 #define numAssert4(expr, v1, v2, v3, v4)   ((expr)?(void)0:nAssertFail(#expr, #v1, v1, #v2, v2, #v3, v3, #v4, v4, __FILE__, __LINE__))
#endif // NDEBUG

#undef assert
#define assert Use_nAssert_instead_of_assert_please

#endif
