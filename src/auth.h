/*
 *  auth.h
 *
 *  Copyright (C) 2003, 2004, 2008 - Niko Ritari
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

#ifndef __AUTH_H_INCLUDED__
#define __AUTH_H_INCLUDED__

#include "map_with_helpers.h"
#include <iostream>
#include <string>
#include <vector>

#include <ctime>

#include "function_utility.h"
#include "network.h"
#include "utility.h"    // for LogSet

class AuthorizationDatabase {
public:
    struct FileError { // exception
        std::string description;
        FileError(const std::string& d) throw () : description(d) { }
    };

    typedef HookFunctionBase1<bool, const std::string&> SettingChecker;

    void load(SettingChecker& validityChecker) throw (FileError);
    void save() const throw (FileError);

    class AccessDescriptor {
    public:
        class GamemodAccessDescriptor {
            struct ControlLine {
                bool allow;
                std::string name;
                ControlLine(bool allow_, const std::string& name_) throw () : allow(allow_), name(name_) { }
            };
            std::vector<ControlLine> lines;
            bool defaultAllow;

        public:
            GamemodAccessDescriptor(bool defaultAllow_) throw () : defaultAllow(defaultAllow_) { }

            void addLine(bool allow, const std::string& name) throw () { lines.push_back(ControlLine(allow, name)); }
            std::ostream& output(std::ostream& os) const throw ();

            bool isAllowed(const std::string& category, const std::string& setting = std::string()) const throw ();
        };

    private:
        friend void AuthorizationDatabase::load(SettingChecker& validityChecker) throw ();
        friend void AuthorizationDatabase::save() const throw ();

        GamemodAccessDescriptor gamemod;
        bool resetAccess;

        bool admin, hasPassword;

        void read(std::istream& in, SettingChecker& validityChecker) throw (FileError);

    public:
        AccessDescriptor() throw () : gamemod(false) { }
        AccessDescriptor(bool allowAll, bool admin_) throw () : gamemod(allowAll), resetAccess(allowAll), admin(admin_), hasPassword(false) { }

        bool isAdmin() const throw () { return admin; }
        bool isProtected() const throw () { return hasPassword; }

        bool canReset() const throw () { return resetAccess; }
        const GamemodAccessDescriptor& gamemodAccess() const throw () { return gamemod; }
    };

private:
    struct NameEntry {
        std::string name;
        std::string password;
        AccessDescriptor access;
        std::string classId;

        NameEntry(const std::string& n, const std::string& p, const AccessDescriptor& a, const std::string& c) throw () : name(n), password(p), access(a), classId(c) { }
    };
    struct BanEntry {
        std::string name;
        Network::Address address;
        time_t endTime;

        BanEntry(const std::string& n, const Network::Address& a, time_t e = time(0) + 365 * 24 * 60 * 60) throw () : name(n), address(a), endTime(e) { }
    };

    std::map<std::string, AccessDescriptor> classes;
    std::vector<NameEntry> names;
    std::vector<BanEntry> bans;

    mutable LogSet log;

    void clear() throw ();

    int identifyName(const std::string& name) const throw ();
    static std::string makeComparable(const std::string& name) throw ();

public:
    AuthorizationDatabase(LogSet logs) throw () : log(logs) { }

    const AccessDescriptor& defaultAccess() const throw () { return map_get_assert(classes, std::string("user")); }
    const AccessDescriptor& shellAccess() const throw () { return map_get_assert(classes, std::string("shell")); }
    const AccessDescriptor& localAccess() const throw () { return map_get_assert(classes, std::string("local")); }
    AccessDescriptor nameAccess(const std::string& name) const throw ();

    bool checkNamePassword(const std::string& name, const std::string& password) const throw ();

    bool isBanned(Network::Address addr) const throw ();
    void ban(Network::Address addr, const std::string& name, int minutes) throw ();
};

inline std::ostream& operator<<(std::ostream& os, const AuthorizationDatabase::AccessDescriptor::GamemodAccessDescriptor& gad) throw () { return gad.output(os); }

#endif
