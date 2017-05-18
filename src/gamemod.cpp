/*
 *  gamemod.cpp
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

#include <sstream>
#include <string>

#include "language.h"
#include "server.h"
#include "servnet.h"
#include "world.h"

// implements:
#include "gamemod.h"

using std::istream;
using std::istringstream;
using std::string;

bool GamemodSetting::basicErrorMessage(LogSet& log, const string& value, const string& expect) {    // always returns false to provide easy returns
    log.error(_("Can't set $1 to '$2' - expecting $3.", name, value, expect));
    return false;
}

bool GS_Boolean::set(LogSet& log, const string& value) {
    const string tval = trim(value);
    if (tval == "1")
        *var = true;
    else if (tval == "0")
        *var = false;
    else
        return basicErrorMessage(log, value, _("0 or 1"));
    return true;
}

bool GS_CheckForwardInt::set(LogSet& log, const string& value) {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();
    istringstream rd(trim(value));
    int val;
    rd >> val;
    if (!rd || rd.peek() != eof_ch || !checkValue(val))
        return basicErrorMessage(log, value, expect);
    return var(val);
}

bool GS_Map::set(LogSet& log, const string& value) {
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

bool GS_PowerupNum::set(LogSet& log, const string& value) {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();
    istringstream rd(trim(value));
    int val;
    rd >> val;
    if (rd && rd.peek() == eof_ch) {
        if (val >= 0 && val <= MAX_PICKUPS) {
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
    return basicErrorMessage(log, value, _("an integer between 0 and $1, or 'n %' with n 0 or greater", itoa(MAX_PICKUPS)));
}

bool GS_Balance::set(LogSet& log, const string& value) {
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

bool GS_Collisions::set(LogSet& log, const string& value) {
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

bool GS_Percentage::set(LogSet& log, const string& value) {
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
