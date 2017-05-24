/*
 *  gamemod.cpp
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
 *  Copyright (C) 2004, 2008 - Jani Rivinoja
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

#include <sstream>
#include <string>

#include "server.h"

// implements:
#include "gamemod.h"

using std::istream;
using std::istringstream;
using std::string;

bool GamemodSetting::basicErrorMessage(LogSet& log, const string& value, const string& expect) throw () {    // always returns false to provide easy returns
    log.error(_("Can't set $1 to '$2' - expecting $3.", name, value, expect));
    return false;
}

bool GS_Boolean::set(LogSet& log, const string& value) throw () {
    const string tval = trim(value);
    if (tval == "1")
        *var = true;
    else if (tval == "0")
        *var = false;
    else
        return basicErrorMessage(log, value, _("0 or 1"));
    return true;
}

bool GS_String::set(LogSet& log, const std::string& value) throw () {
    if (!allowEmpty && value.find_first_not_of(" \t"))
        return basicErrorMessage(log, value, _("non-empty string"));
    *var = value;
    return true;
}

bool GS_CheckForwardInt::set(LogSet& log, const string& value) throw () {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();
    istringstream rd(trim(value));
    int val;
    rd >> val;
    if (!rd || rd.peek() != eof_ch || !checkValue(val))
        return basicErrorMessage(log, value, expect);
    return setFn(val);
}

string GS_CheckForwardInt::get() throw () {
    return itoa(getFn());
}

bool GS_Map::set(LogSet& log, const string& value) throw () {
    MapInfo mi;
    if (mi.load(log, trim(value))) {
        var->push_back(mi);
        log("Added '%s' to map rotation.", value.c_str());
        return true;
    }
    else {
        log.error(_("Can't add '$1' to map rotation.", value));
        return false;
    }
}

bool GS_RandomMap::set(LogSet& log, const string& value) throw () {
    istringstream ist(value);
    MapInfo mi;
    ist >> mi.width >> mi.height;
    const bool ok = ist;
    float over_edge = 0.2;
    ist >> over_edge;
    if (ok && mi.width > 0 && mi.height > 0 && over_edge >= 0 && over_edge <= 1) {
        mi.author = "Outgun";
        mi.title = "<Random>";
        mi.random = true;
        mi.over_edge = over_edge;
        var->push_back(mi);
        log("Added a random %d×%d map to map rotation.", mi.width, mi.height);
        return true;
    }
    else
        return basicErrorMessage(log, value, _("two positive integers and optionally a real number between 0 and 1, separated by spaces"));
}

bool GS_PowerupNum::set(LogSet& log, const string& value) throw () {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();
    istringstream rd(trim(value));
    int val;
    rd >> val;
    if (rd && rd.peek() == eof_ch) {
        if (val >= 0 && val <= MAX_POWERUPS) {
            *var = val;
            *percentFlag = false;
            return true;
        }
    }
    else {
        char ch;
        rd >> ch;
        if (rd && ch == '%' && rd.peek() == eof_ch && val >= 0) {
            *var = val;
            *percentFlag = true;
            return true;
        }
    }
    return basicErrorMessage(log, value, _("an integer between 0 and $1, or 'n %' with n 0 or greater", itoa(MAX_POWERUPS)));
}

string GS_PowerupNum::get() throw () {
    if (*percentFlag)
        return itoa(*var) + " %";
    else
        return itoa(*var);
}

bool GS_Balance::set(LogSet& log, const string& value) throw () {
    const string tval = trim(value);
    if (tval == "no")
        *var = WorldSettings::TB_disabled;
    else if (tval == "balance")
        *var = WorldSettings::TB_balance;
    else if (tval == "shuffle")
        *var = WorldSettings::TB_balance_and_shuffle;
    else
        return basicErrorMessage(log, value, _("one of no, balance, and shuffle"));
    return true;
}

string GS_Balance::get() throw () {
    switch (*var) {
    /*break;*/ case WorldSettings::TB_disabled:            return "no";
        break; case WorldSettings::TB_balance:             return "balance";
        break; case WorldSettings::TB_balance_and_shuffle: return "shuffle";
        break; default: nAssert(0);
    }
}

bool GS_Collisions::set(LogSet& log, const string& value) throw () {
    const string tval = trim(value);
    if (tval == "no" || tval == "0")
        *var = PhysicalSettings::PC_none;
    else if (tval == "normal" || tval == "1")
        *var = PhysicalSettings::PC_normal;
    else if (tval == "special" || tval == "2")
        *var = PhysicalSettings::PC_special;
    else
        return basicErrorMessage(log, value, _("one of no, normal, and special"));
    return true;
}

string GS_Collisions::get() throw () {
    switch (*var) {
    /*break;*/ case PhysicalSettings::PC_none:    return "no";
        break; case PhysicalSettings::PC_normal:  return "normal";
        break; case PhysicalSettings::PC_special: return "special";
        break; default: nAssert(0);
    }
}

bool GS_Percentage::set(LogSet& log, const string& value) throw () {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();
    istringstream rd(trim(value));
    double val;
    rd >> val;
    if (rd && rd.peek() == eof_ch) {
        if (val >= 0.) {
            *var = val;
            return true;
        }
    }
    else {
        char ch;
        rd >> ch;
        if (rd && ch == '%' && rd.peek() == eof_ch && val >= 0.) {
            *var = val / 100.;
            return true;
        }
    }
    return basicErrorMessage(log, value, _("a real number or 'x %' with x 0 or greater"));
}

string GS_Percentage::get() throw () {
    const int perc = static_cast<int>(*var * 100. + .5);
    if (perc != 0 && fabs(*var * 100. - perc) < .01)
        return itoa(perc) + " %";
    else
        return fcvt(*var);
}
