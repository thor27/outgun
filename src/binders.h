/*
 *  binders.h
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

#ifndef BINDERS_H_INC
#define BINDERS_H_INC

/* binder_def.inc is used to produce template classes of the following outline, with a varying
 * number of arguments and corresponding changes in the class and function names:
 */

#if 0

/// Bind this-object and arguments to a member function with 2 arguments, producing a nullary function.
template<class RetT, class ObjT, class MemFnT, class Arg1T, class Arg2T> class MF2binder {
    ObjT* p;
    MemFnT f;
    Arg1T a1; Arg2T a2;

public:
    MF2binder(ObjT& o, const MemFnT& f_, const Arg1T& a1_, const Arg2T& a2_) throw () : p(&o), f(f_), a1(a1_), a2(a2_) { }
    RetT operator()() const { return (p->*f)(a1, a2); }
};

template<class ObjT, class MemFnT, class Arg1T, class Arg2T>
MF2binder<void, ObjT, MemFnT, Arg1T, Arg2T> VMFbind2(ObjT& o, const MemFnT& f, const Arg1T& a1, const Arg2T& a2) throw () {
    return MF2binder<void, ObjT, MemFnT, Arg1T, Arg2T>(o, f, a1, a2);
}

// use like:

class ObjT {
public:
    RetT memfn(Arg1T, Arg2T);
    void void_memfn(Arg1T, Arg2T);
};

void f() {
    ObjT o;
    Arg1T a1;
    Arg2T a2;

    MF2binder<RetT, ObjT, MemFnT, Arg1T, Arg2T> binder(o, &ObjT::memfn, a1, a2);
    RetT t = binder(); // equal to: RetT t = o.memfn(a1, a2);

    binder2 = VMFbind2(o, &ObjT::void_memfn, a1, a2); // bind to void member function (VMF) without having to name the type
}

#endif

#define MAKE_NAME(a, b) a##0##b
#define ARG_LIST(a, n)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##1##b
#define ARG_LIST(a, n) a##_ARG(1)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##2##b
#define ARG_LIST(a, n) a##_ARG(1) n##_NEXT a##_ARG(2)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##3##b
#define ARG_LIST(a, n) a##_ARG(1) n##_NEXT a##_ARG(2) n##_NEXT a##_ARG(3)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##4##b
#define ARG_LIST(a, n) a##_ARG(1) n##_NEXT a##_ARG(2) n##_NEXT a##_ARG(3) n##_NEXT a##_ARG(4)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##5##b
#define ARG_LIST(a, n) a##_ARG(1) n##_NEXT a##_ARG(2) n##_NEXT a##_ARG(3) n##_NEXT a##_ARG(4) n##_NEXT a##_ARG(5)
#include "binder_def.inc"

#define MAKE_NAME(a, b) a##6##b
#define ARG_LIST(a, n) a##_ARG(1) n##_NEXT a##_ARG(2) n##_NEXT a##_ARG(3) n##_NEXT a##_ARG(4) n##_NEXT a##_ARG(5) n##_NEXT a##_ARG(6)
#include "binder_def.inc"

// binder_def.inc adds macro pollution without #undefing; if anything is added here, add the #undefs there

#endif // BINDERS_H_INC
