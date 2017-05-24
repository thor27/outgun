/*
 *  function_utility.h
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

#ifndef FUNCTION_UTILITY_INC
#define FUNCTION_UTILITY_INC

// function_utility.h : template classes to wrap different kinds of function objects

// HookFunctionBase# provides a common interface for storing and accessing function references
// HookFunctionHolder# manages a pointer to HookFunctionBase and behaves just like a regular function pointer would
// Hook# is a HookFunctionHolder#-like object that can be unbound and provides a different (more explicit) call syntax
// Hookable# is a base class to allow easy inclusion of a Hook# in a class (use with care)

// RedirectToFun# (deriving from HookFunctionBase#) is a helper giving a regular function pointer the interface of HookFunctionBase#
// RedirectToMemFun# (deriving from HookFunctionBase#) is a helper allowing function objects bound to a member function of an object

// generating new versions with different amounts of arguments should be easy when needed; shame creating them can't be circumvented


// 0-argument HookFunctionBase#, Hook#, Hookable# :

template<class RetT>
class HookFunctionBase0 {
public:
    virtual ~HookFunctionBase0() throw () { }
    virtual RetT operator()() const throw () = 0;
    virtual HookFunctionBase0* clone() const throw () = 0;
};

template<class RetT>
class HookFunctionHolder0 {
public:
    typedef HookFunctionHolder0 ThisT;
    typedef HookFunctionBase0<RetT> FunctionT;

    HookFunctionHolder0(FunctionT* fn) throw () : hookFn(fn) { }
    HookFunctionHolder0(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~HookFunctionHolder0() throw () { delete hookFn; }
    void operator=(const ThisT& o) throw () { delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()() const throw () { return (*hookFn)(); }

private:
    FunctionT* hookFn;
};

template<class RetT>
class Hook0 {
public:
    typedef HookFunctionBase0<RetT> FunctionT;

    Hook0() throw () : hookFn(0) { }
    ~Hook0() throw () { free(); }
    void set(FunctionT* fn) throw () { free(); hookFn = fn; }    // the ownership is transferred
    bool active() const throw () { return hookFn != 0; }
    RetT call() const throw () { if (hookFn) return (*hookFn)(); else return RetT(); }

private:
    void free() throw () { if (hookFn) delete hookFn; }

    FunctionT* hookFn;
};

template<class RetT>
class Hookable0 {
public:
    typedef typename Hook0<RetT>::FunctionT HookFunctionT;

    virtual ~Hookable0() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook() const throw () { return hook.call(); }

private:
    Hook0<RetT> hook;
};

// 1-argument HookFunctionBase#, Hook#, Hookable# :

template<class RetT, class Arg1T>
class HookFunctionBase1 {
public:
    virtual ~HookFunctionBase1() throw () { }
    virtual RetT operator()(Arg1T) const throw () = 0;
    virtual HookFunctionBase1* clone() const throw () = 0;
};

template<class RetT, class Arg1T>
class HookFunctionHolder1 {
public:
    typedef HookFunctionHolder1 ThisT;
    typedef HookFunctionBase1<RetT, Arg1T> FunctionT;

    HookFunctionHolder1(FunctionT* fn) throw () : hookFn(fn) { }
    HookFunctionHolder1(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~HookFunctionHolder1() throw () { delete hookFn; }
    void operator=(const ThisT& o) throw () { delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1) const throw () { return (*hookFn)(a1); }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T>
class Hook1 {
public:
    typedef HookFunctionBase1<RetT, Arg1T> FunctionT;

    Hook1() throw () : hookFn(0) { }
    ~Hook1() throw () { free(); }
    void set(FunctionT* fn) throw () { free(); hookFn = fn; }    // the ownership is transferred
    bool active() const throw () { return hookFn != 0; }
    RetT call(Arg1T a1) const throw () { if (hookFn) return (*hookFn)(a1); else return RetT(); }

private:
    void free() throw () { if (hookFn) delete hookFn; }

    FunctionT* hookFn;
};

template<class RetT, class Arg1T>
class Hookable1 {
public:
    typedef typename Hook1<RetT, Arg1T>::FunctionT HookFunctionT;

    virtual ~Hookable1() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1) const throw () { return hook.call(a1); }

private:
    Hook1<RetT, Arg1T> hook;
};

// 2-argument HookFunctionBase#, Hook#, Hookable# :

template<class RetT, class Arg1T, class Arg2T>
class HookFunctionBase2 {
public:
    virtual ~HookFunctionBase2() throw () { }
    virtual RetT operator()(Arg1T, Arg2T) const throw () = 0;
    virtual HookFunctionBase2* clone() const throw () = 0;
};

template<class RetT, class Arg1T, class Arg2T>
class HookFunctionHolder2 {
public:
    typedef HookFunctionHolder2 ThisT;
    typedef HookFunctionBase2<RetT, Arg1T, Arg2T> FunctionT;

    HookFunctionHolder2(FunctionT* fn) throw () : hookFn(fn) { }
    HookFunctionHolder2(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~HookFunctionHolder2() throw () { delete hookFn; }
    void operator=(const ThisT& o) throw () { delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1, Arg2T a2) const throw () { return (*hookFn)(a1, a2); }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T>
class Hook2 {
public:
    typedef HookFunctionBase2<RetT, Arg1T, Arg2T> FunctionT;

    Hook2() throw () : hookFn(0) { }
    ~Hook2() throw () { free(); }
    void set(FunctionT* fn) throw () { free(); hookFn = fn; }    // the ownership is transferred
    bool active() const throw () { return hookFn != 0; }
    RetT call(Arg1T a1, Arg2T a2) const throw () { if (hookFn) return (*hookFn)(a1, a2); else return RetT(); }

private:
    void free() throw () { if (hookFn) delete hookFn; }

    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T>
class Hookable2 {
public:
    typedef typename Hook2<RetT, Arg1T, Arg2T>::FunctionT HookFunctionT;

    virtual ~Hookable2() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1, Arg2T a2) const throw () { return hook.call(a1, a2); }

private:
    Hook2<RetT, Arg1T, Arg2T> hook;
};

// 3-argument HookFunctionBase#, Hook#, Hookable# :

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class HookFunctionBase3 {
public:
    virtual ~HookFunctionBase3() throw () { }
    virtual RetT operator()(Arg1T, Arg2T, Arg3T) const throw () = 0;
    virtual HookFunctionBase3* clone() const throw () = 0;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class HookFunctionHolder3 {
public:
    typedef HookFunctionHolder3 ThisT;
    typedef HookFunctionBase3<RetT, Arg1T, Arg2T, Arg3T> FunctionT;

    HookFunctionHolder3(FunctionT* fn) throw () : hookFn(fn) { }
    HookFunctionHolder3(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~HookFunctionHolder3() throw () { delete hookFn; }
    void operator=(const ThisT& o) throw () { delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { return (*hookFn)(a1, a2, a3); }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class Hook3 {
public:
    typedef HookFunctionBase3<RetT, Arg1T, Arg2T, Arg3T> FunctionT;

    Hook3() throw () : hookFn(0) { }
    ~Hook3() throw () { free(); }
    void set(FunctionT* fn) throw () { free(); hookFn = fn; }    // the ownership is transferred
    bool active() const throw () { return hookFn != 0; }
    RetT call(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { if (hookFn) return (*hookFn)(a1, a2, a3); else return RetT(); }

private:
    void free() throw () { if (hookFn) delete hookFn; }

    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class Hookable3 {
public:
    typedef typename Hook3<RetT, Arg1T, Arg2T, Arg3T>::FunctionT HookFunctionT;

    virtual ~Hookable3() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { return hook.call(a1, a2, a3); }

private:
    Hook3<RetT, Arg1T, Arg2T, Arg3T> hook;
};

// 0-argument RedirectToMemFun

template<class HostClass, class ReturnT>
class RedirectToMemFun0 : public HookFunctionBase0<ReturnT> {
    HostClass* host;
    ReturnT (HostClass::*function)() throw ();

public:
    RedirectToMemFun0(HostClass* h, ReturnT (HostClass::*memFun)() throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()() const throw () { return (host->*function)(); }
    RedirectToMemFun0* clone() const throw () { return new RedirectToMemFun0(host, function); }
};

template<class HostClass, class ReturnT>
class RedirectToConstMemFun0 : public HookFunctionBase0<ReturnT> {
    const HostClass* host;
    ReturnT (HostClass::*function)() const throw ();

public:
    RedirectToConstMemFun0(const HostClass* h, ReturnT (HostClass::*memFun)() const throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()() const throw () { return (host->*function)(); }
    RedirectToConstMemFun0* clone() const throw () { return new RedirectToConstMemFun0(host, function); }
};

template<class ReturnT>
class RedirectToFun0 : public HookFunctionBase0<ReturnT> {
    ReturnT (*function)() throw ();

public:
    RedirectToFun0(ReturnT (*fun)() throw ()) throw () : function(fun) { }
    ReturnT operator()() const throw () { return (*function)(); }
    RedirectToFun0* clone() const throw () { return new RedirectToFun0(function); }
};

template<class HostClass, class ReturnT>
RedirectToMemFun0<HostClass, ReturnT>* newRedirectToMemFun0(HostClass* h, ReturnT (HostClass::*memFun)() throw ()) throw () { return new RedirectToMemFun0<HostClass, ReturnT>(h, memFun); }

template<class HostClass, class ReturnT>
RedirectToConstMemFun0<HostClass, ReturnT>* newRedirectToConstMemFun0(const HostClass* h, ReturnT (HostClass::*memFun)() const throw ()) throw () { return new RedirectToConstMemFun0<HostClass, ReturnT>(h, memFun); }

template<class ReturnT>
RedirectToFun0<ReturnT>* newRedirectToFun0(ReturnT (*function)() throw ()) throw () { return new RedirectToFun0<ReturnT>(function); }

// 1-argument RedirectToMemFun

template<class HostClass, class ReturnT, class Arg1T>
class RedirectToMemFun1 : public HookFunctionBase1<ReturnT, Arg1T> {
    HostClass* host;
    ReturnT (HostClass::*function)(Arg1T) throw ();

public:
    RedirectToMemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (host->*function)(arg); }
    RedirectToMemFun1* clone() const throw () { return new RedirectToMemFun1(host, function); }
};

template<class HostClass, class ReturnT, class Arg1T>
class RedirectToConstMemFun1 : public HookFunctionBase1<ReturnT, Arg1T> {
    const HostClass* host;
    ReturnT (HostClass::*function)(Arg1T) const throw ();

public:
    RedirectToConstMemFun1(const HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) const throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (host->*function)(arg); }
    RedirectToConstMemFun1* clone() const throw () { return new RedirectToConstMemFun1(host, function); }
};

template<class ReturnT, class Arg1T>
class RedirectToFun1 : public HookFunctionBase1<ReturnT, Arg1T> {
    ReturnT (*function)(Arg1T) throw ();

public:
    RedirectToFun1(ReturnT (*fun)(Arg1T) throw ()) throw () : function(fun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (*function)(arg); }
    RedirectToFun1* clone() const throw () { return new RedirectToFun1(function); }
};

template<class HostClass, class ReturnT, class Arg1T>
RedirectToMemFun1<HostClass, ReturnT, Arg1T>* newRedirectToMemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () { return new RedirectToMemFun1<HostClass, ReturnT, Arg1T>(h, memFun); }

template<class HostClass, class ReturnT, class Arg1T>
RedirectToConstMemFun1<HostClass, ReturnT, Arg1T>* newRedirectToConstMemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () { return new RedirectToConstMemFun1<HostClass, ReturnT, Arg1T>(h, memFun); }

template<class ReturnT, class Arg1T>
RedirectToFun1<ReturnT, Arg1T>* newRedirectToFun1(ReturnT (*function)(Arg1T) throw ()) throw () { return new RedirectToFun1<ReturnT, Arg1T>(function); }

// 2-argument RedirectToMemFun

template<class HostClass, class ReturnT, class Arg1T, class Arg2T>
class RedirectToMemFun2 : public HookFunctionBase2<ReturnT, Arg1T, Arg2T> {
    HostClass* host;
    ReturnT (HostClass::*function)(Arg1T, Arg2T) throw ();

public:
    RedirectToMemFun2(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T, Arg2T) throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg1, Arg2T arg2) const throw () { return (host->*function)(arg1, arg2); }
};

template<class ReturnT, class Arg1T, class Arg2T>
class RedirectToFun2 : public HookFunctionBase2<ReturnT, Arg1T, Arg2T> {
    ReturnT (*function)(Arg1T, Arg2T) throw ();

public:
    RedirectToFun2(ReturnT (*fun)(Arg1T, Arg2T) throw ()) : function(fun) { }
    ReturnT operator()(Arg1T arg1, Arg2T arg2) const throw () { return (*function)(arg1, arg2); }
    RedirectToFun2* clone() const throw () { return new RedirectToFun2(function); }
};

template<class HostClass, class ReturnT, class Arg1T, class Arg2T>
HookFunctionBase2<ReturnT, Arg1T, Arg2T>* newRedirectToMemFun2(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T, Arg2T) throw ()) throw () { return new RedirectToMemFun2<HostClass, ReturnT, Arg1T, Arg2T>(h, memFun); }

template<class ReturnT, class Arg1T, class Arg2T>
RedirectToFun2<ReturnT, Arg1T, Arg2T>* newRedirectToFun2(ReturnT (*function)(Arg1T, Arg2T) throw ()) throw () { return new RedirectToFun2<ReturnT, Arg1T, Arg2T>(function); }

// 0-argument HookFnStripConstRef0

template<class RetT>
class HookFnStripConstRef0 : public HookFunctionBase0<RetT> {
    HookFunctionBase0<const RetT&>& base;

public:
    HookFnStripConstRef0(HookFunctionBase0<const RetT&>& base_) throw () : base(base_) { }
    RetT operator()() const throw () { return base(); }
    HookFnStripConstRef0* clone() const throw () { return new HookFnStripConstRef0(base); }
};

template<class RetT>
HookFnStripConstRef0<RetT>* newHookFnStripConstRef0(HookFunctionBase0<const RetT&>& base_) throw () { return new HookFnStripConstRef0<RetT>(base_); }

// simple utility classes that utilize hook functions

class AtScopeExit {
    HookFunctionHolder0<void> action;

public:
    AtScopeExit(HookFunctionBase0<void>* action_) throw () : action(action_) { }
    ~AtScopeExit() throw () { action(); }
};

#endif
