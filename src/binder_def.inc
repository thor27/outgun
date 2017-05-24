/*
 *  binder_def.inc
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

// This file is only intended to be included from binders.h.

/* Mad preprocessor magic is used to define template class MF#binder and bind#(), for a given # representing the number of arguments.
 * The following macros must be defined before inclusion (and will be undefined here after use):
 *   MAKE_NAME(a, b)  a ## <number-of-args> ## b
 *   ARG_LIST(a, n)  a##_ARG(1) n##_NEXT a##_ARG(2) n##_NEXT a##_ARG(3)  with appropriate number of ARGs and NEXTs between each two
 */

// COMMA_NEXT is used where commas are needed only between successive expansions; if also before the first expansion (but only if there are expansions), comma is to be in *_ARG instead
#define COMMA_NEXT ,
#define PLAIN_NEXT

#define    CLASS_ARG(n) , class Arg##n##T                 // , class Arg1T
#define TYPENAME_ARG(n) ,       Arg##n##T                 // , Arg1T
#define  MEMDECL_ARG(n)         Arg##n##T  a##n;          // Arg1T a1;
#define  CONSARG_ARG(n) , const Arg##n##T&      a##n##_   // , const Arg1T& a1_
#define  MEMINIT_ARG(n) ,                  a##n(a##n##_)  // , a1(a1_)
#define EXTFNARG_ARG(n) , const Arg##n##T& a##n           // , const Arg1T& a1
#define     CALL_ARG(n)                    a##n           // a1
#define    CCALL_ARG(n) ,                  a##n           // , a1

template<class RetT, class ObjT, class MemFnT ARG_LIST(CLASS, PLAIN)> class MAKE_NAME(MF, binder) {
    ObjT* p;
    MemFnT f;
    ARG_LIST(MEMDECL, PLAIN)

public:
    MAKE_NAME(MF, binder)(ObjT& o, const MemFnT& f_ ARG_LIST(CONSARG, PLAIN)) throw () : p(&o), f(f_) ARG_LIST(MEMINIT, PLAIN) { }
    RetT operator()() const { return (p->*f)(ARG_LIST(CALL, COMMA)); }
};

template<class ObjT, class MemFnT ARG_LIST(CLASS, PLAIN)>
MAKE_NAME(MF, binder)<void, ObjT, MemFnT ARG_LIST(TYPENAME, PLAIN)> MAKE_NAME(VMFbind,)(ObjT& o, const MemFnT& f ARG_LIST(EXTFNARG, PLAIN)) throw () {
    return MAKE_NAME(MF, binder)<void, ObjT, MemFnT ARG_LIST(TYPENAME, PLAIN)>(o, f ARG_LIST(CCALL, PLAIN));
}

#undef MAKE_NAME
#undef ARG_LIST
