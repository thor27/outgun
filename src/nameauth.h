/*
 *  nameauth.h
 *
 *  Copyright (C) 2003, 2004 - Niko Ritari
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

#ifndef __NAMEAUTH_H_INCLUDED__
#define __NAMEAUTH_H_INCLUDED__

#include <ostream>
#include <string>
#include <vector>

#include <ctime>

#include <nl.h>

#include "utility.h"    // for LogSet

class NameAuthorizationDatabase {
    struct NameEntry {
        std::string name;
        std::string password;
        bool admin;

        NameEntry(const std::string& n, const std::string& p, bool a) : name(n), password(p), admin(a) { }
    };
    struct BanEntry {
        std::string name;
        NLaddress address;
        time_t endTime;

        BanEntry(const std::string& n, const NLaddress& a, time_t e = time(0) + 365 * 24 * 60 * 60) : name(n), address(a), endTime(e) { }
    };

    std::vector<NameEntry> names;
    std::vector<BanEntry> bans;

    mutable LogSet log;

    int identifyName(const std::string& name) const;
    static std::string makeComparable(const std::string& name);

public:
    NameAuthorizationDatabase(LogSet logs) : log(logs) { }

    void clear();
    bool load();
    bool save() const;

    bool isProtected(const std::string& name) const;
    bool checkNamePassword(const std::string& name, const std::string& password) const;
    bool isAdmin(const std::string& name) const;

    bool isBanned(NLaddress addr) const;
    void ban(NLaddress addr, const std::string& name, int minutes);
};

#endif
