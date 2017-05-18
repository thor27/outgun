/*
 *  gamemod.h
 *
 *  Copyright (C) 2004 - Niko Ritari
 *  Copyright (C) 2004 - Jani Rivinoja
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

#ifndef GAMEMOD_H_INC
#define GAMEMOD_H_INC

#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "function_utility.h"
#include "language.h"
#include "world.h"  // for WorldSettings::Team_balance

class LogSet;
class MapInfo;

class GamemodSetting {  // base class
public:
    virtual ~GamemodSetting() { }
    virtual bool set(LogSet& log, const std::string& value) = 0;    // returns false if the value is not accepted
    bool matchCommand(const std::string& command) const { return command == name; }

protected:
    GamemodSetting(std::string name_) : name(name_) { }
    bool basicErrorMessage(LogSet& log, const std::string& value, const std::string& expect);   // always returns false to provide easy returns

    std::string name;
};

// generic setting prototypes

class GS_Ignore : public GamemodSetting {
public:
    GS_Ignore(const std::string& name) : GamemodSetting(name) { }
    bool set(LogSet&, const std::string&) { return true; }
};

// deal with Visual C++'s perks
#undef min
#undef max

template<class ValT>
class GS_IntT : public GamemodSetting {
public:
    typedef std::numeric_limits<ValT> lim;  // can't be used on the constructor, avoid a VC feature

    GS_IntT(const std::string& name, ValT* pVar, ValT min_ = std::numeric_limits<ValT>::min(),
            ValT max_ = std::numeric_limits<ValT>::max(), int mul_ = 1, int add_ = 0, bool allow0_ = false)
        : GamemodSetting(name), var(pVar), vmin(min_), vmax(max_), mul(mul_), add(add_), allow0(allow0_) { }
    virtual ~GS_IntT() { }
    bool set(LogSet& log, const std::string& value);

private:
    ValT* var;
    ValT vmin, vmax;
    int mul, add;
    bool allow0;
};

template<class ValT>
class GS_ForwardIntT : public GS_IntT<ValT> {
public:
    GS_ForwardIntT(const std::string& name, HookFunctionBase1<void, int>& pVar, ValT min_ = std::numeric_limits<ValT>::min(),
                   ValT max_ = std::numeric_limits<ValT>::max(), int mul_ = 1, int add_ = 0, bool allow0 = false)
        : GS_IntT<ValT>(name, &internalVal, min_, max_, mul_, add_, allow0), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    HookFunctionBase1<void, int>& var;
    ValT internalVal;
};

typedef GS_IntT<int> GS_Int;
typedef GS_IntT<NLulong> GS_Ulong;

typedef GS_ForwardIntT<int> GS_ForwardInt;

template<class ValT>
class GS_FloatT : public GamemodSetting {
public:
    typedef std::numeric_limits<ValT> lim;  // can't be used on the constructor, avoid a VC feature

    GS_FloatT(const std::string& name, ValT* pVar, ValT min_ = -std::numeric_limits<ValT>::max(),
            ValT max_ = std::numeric_limits<ValT>::max(), double mul_ = 1., double add_ = 0.)
        : GamemodSetting(name), var(pVar), vmin(min_), vmax(max_), mul(mul_), add(add_) { }
    bool set(LogSet& log, const std::string& value);

private:
    ValT* var;
    ValT vmin, vmax;
    double mul, add;
};

//typedef GS_FloatT<float> GS_Float;    // use of float should be avoided because of a GCC feature
typedef GS_FloatT<double> GS_Double;

class GS_Boolean : public GamemodSetting {
public:
    GS_Boolean(const std::string& name, bool* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    bool* var;
};

class GS_String : public GamemodSetting {
public:
    GS_String(const std::string& name, std::string* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet&, const std::string& value) {
        *var = value;
        return true;
    }

private:
    std::string* var;
};

class GS_AddString : public GamemodSetting {
public:
    GS_AddString(const std::string& name, std::vector<std::string>* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet&, const std::string& value) {
        var->push_back(value);
        return true;
    }

private:
    std::vector<std::string>* var;
};

class GS_ForwardStr : public GamemodSetting {
public:
    GS_ForwardStr(const std::string& name, HookFunctionBase1<void, const std::string&>& pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet&, const std::string& value) {
        var(value);
        return true;
    }

private:
    HookFunctionBase1<void, const std::string&>& var;
};

class GS_CheckForwardStr : public GamemodSetting {
public:
    GS_CheckForwardStr(const std::string& name, const std::string& expect_, HookFunctionBase1<bool, const std::string&>& check_, HookFunctionBase1<bool, const std::string&>& pVar)
        : GamemodSetting(name), expect(expect_), checkValue(check_), var(pVar) { }
    bool set(LogSet& log, const std::string& value) {
        if (!checkValue(value))
            return basicErrorMessage(log, value, expect);
        return var(value);
    }

private:
    const std::string expect;
    HookFunctionBase1<bool, const std::string&>& checkValue;
    HookFunctionBase1<bool, const std::string&>& var;
};

class GS_CheckForwardInt : public GamemodSetting {
public:
    GS_CheckForwardInt(const std::string& name, const std::string& expect_, HookFunctionBase1<bool, int>& check_, HookFunctionBase1<bool, int>& pVar)
        : GamemodSetting(name), expect(expect_), checkValue(check_), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    const std::string expect;
    HookFunctionBase1<bool, int>& checkValue;
    HookFunctionBase1<bool, int>& var;
};

// specific settings that require special handling

class GS_DisallowRunning : public GamemodSetting {
public:
    GS_DisallowRunning(const std::string& name) : GamemodSetting(name) { }
    bool set(LogSet& log, const std::string&) {
        log("Skipping %s; restart server to make a change", name.c_str());
        return true;    // this is not really an error; the value might have not changed even
    }
};

class GS_Map : public GamemodSetting {
public:
    GS_Map(const std::string& name, std::vector<MapInfo>* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    std::vector<MapInfo>* var;
};

class GS_PowerupNum : public GamemodSetting {
public:
    GS_PowerupNum(const std::string& name, int* pVar, bool* pPercentFlag) : GamemodSetting(name), var(pVar), percentFlag(pPercentFlag) { }
    bool set(LogSet& log, const std::string& value);

private:
    int* var;
    bool* percentFlag;
};

class GS_Balance : public GamemodSetting {
public:
    GS_Balance(const std::string& name, WorldSettings::Team_balance* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    WorldSettings::Team_balance* var;
};

class GS_Collisions : public GamemodSetting {
public:
    GS_Collisions(const std::string& name, PhysicalSettings::PlayerCollisions* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    PhysicalSettings::PlayerCollisions* var;
};

class GS_Percentage : public GamemodSetting {
public:
    GS_Percentage(const std::string& name, double* pVar) : GamemodSetting(name), var(pVar) { }
    bool set(LogSet& log, const std::string& value);

private:
    double* var;
};

// template implementation

template<class ValT>
bool GS_IntT<ValT>::set(LogSet& log, const std::string& value) {
    static const std::istream::traits_type::int_type eof_ch = std::istream::traits_type::eof();
    std::istringstream rd(trim(value));
    ValT val;
    rd >> val;
    if (!rd || rd.peek() != eof_ch || ((val < vmin || val > vmax) && !(val == 0 && allow0))) {
        std::string orZero;
        if (allow0)
            orZero = _(", or 0");
        const bool minBound = (vmin != lim::min() || static_cast<double>(lim::min()) > -100000.);   // compare doubles to avoid range and signedness warnings
        const bool maxBound = (vmax != lim::max() || static_cast<double>(lim::max()) <  100000.);
        if (minBound && maxBound) {
            if (vmax == vmin + 1)
                return basicErrorMessage(log, value, _("$1 or $2", itoa(vmin), itoa(vmax)) + orZero);
            else
                return basicErrorMessage(log, value, _("an integer, between $1 and $2, inclusive", itoa(vmin), itoa(vmax)) + orZero);
        }
        else if (maxBound)
            return basicErrorMessage(log, value, _("an integer, at most $1", itoa(vmax)) + orZero);
        else if (minBound)
            return basicErrorMessage(log, value, _("an integer, at least $1", itoa(vmin)) + orZero);
        else
            return basicErrorMessage(log, value, _("an integer"));
        return false;
    }
    *var = val * mul + add;
    return true;
}

template<class ValT>
bool GS_ForwardIntT<ValT>::set(LogSet& log, const std::string& value) {
    if (!GS_IntT<ValT>::set(log, value))
        return false;
    var(internalVal);
    return true;
}

template<class ValT>
bool GS_FloatT<ValT>::set(LogSet& log, const std::string& value) {
    static const std::istream::traits_type::int_type eof_ch = std::istream::traits_type::eof();
    std::istringstream rd(trim(value));
    ValT val;
    rd >> val;
    if (!rd || rd.peek() != eof_ch || val < vmin || val > vmax) {
        if (vmin == -lim::max() && vmax == lim::max())
            return basicErrorMessage(log, value, _("a real number"));
        else if (vmin == -lim::max())
            return basicErrorMessage(log, value, _("a real number, at most $1", fcvt(vmax)));
        else if (vmax == lim::max())
            return basicErrorMessage(log, value, _("a real number, at least $1", fcvt(vmin)));
        else
            return basicErrorMessage(log, value, _("a real number, between $1 and $2, inclusive", fcvt(vmin), fcvt(vmax)));
    }
    *var = val * mul + add;
    return true;
}

#endif
