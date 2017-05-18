/*
 *  nameauth.cpp
 *
 *  Copyright (C) 2003, 2004, 2006 - Niko Ritari
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

#include <fstream>
#include <sstream>

#include <nl.h>

#include "commont.h"
#include "language.h"
#include "nameauth.h"
#include "nassert.h"
#include "network.h"

using std::ifstream;
using std::istringstream;
using std::ofstream;
using std::ostream;
using std::string;
using std::vector;

string NameAuthorizationDatabase::makeComparable(const string& name) {
    string nameUpr;
    for (string::size_type i = 0; i < name.length(); ++i) {
        if (isalnum(name[i]))
            nameUpr += char(toupper(name[i]));
        else if (!nameUpr.empty())
            nameUpr += '.';
    }
    const string::size_type endi = nameUpr.find_last_not_of('.');
    if (endi == string::npos)
        return string();
    return nameUpr.substr(0, endi + 1);
}

void NameAuthorizationDatabase::clear() {
    names.clear();
    bans.clear();
}

bool NameAuthorizationDatabase::load() {
    clear();
    const string filename = wheregamedir + "config" + directory_separator + "auth.txt";
    ifstream in(filename.c_str());
    if (!in) {
        log.error(_("Can't read '$1'.", filename));
        return false;
    }
    bool bansChanged = false;
    for (;;) {
        string line;
        if (!getline_skip_comments(in, line))
            break;
        istringstream strl(line);
        string command, name, data;
        strl >> command;
        strl.ignore();  // useful especially when the separator is a tab
        getline(strl, name, '\t');
        const string compName = makeComparable(name);
        if (!strl || compName.empty()) {
            log.error(_("Invalid line (no name) in auth.txt: \"$1\"", line));
            continue;
        }
        strl >> data;
        const bool dataRead = strl;
        command = toupper(command);
        if (command == "USER") {
            if (!dataRead)
                log.error(_("Invalid user command (no password) in auth.txt: \"$1\"", line));
            else
                names.push_back(NameEntry(compName, data, false));
        }
        else if (command == "ADMIN") {
            if (dataRead)
                names.push_back(NameEntry(compName, data, true));
            else
                names.push_back(NameEntry(compName, "", true));
        }
        else if (command == "BAN") {
            if (!dataRead || !isValidIP(data))
                log.error(_("Invalid ban command (IP address) in auth.txt: \"$1\"", line));
            else {
                NLaddress addr;
                if (!nlStringToAddr(data.c_str(), &addr))
                    nAssert(0);
                nlSetAddrPort(&addr, 0);
                time_t endTime;
                strl >> endTime;
                if (!strl)
                    bans.push_back(BanEntry(name, addr)); // use name instead of compName because names in bans don't need to be compared
                else if (endTime > time(0))
                    bans.push_back(BanEntry(name, addr, endTime));
                else    // if the ban isn't in effect any more, remove from the list
                    bansChanged = true;
            }
        }
        else
            log.error(_("Unrecognized command \"$1\" in auth.txt", command));
    }
    if (bansChanged)
        return save();
    else
        return true;
}

bool NameAuthorizationDatabase::save() const {
    const string filename = wheregamedir + "config" + directory_separator + "auth.txt";
    ofstream out(filename.c_str());
    if (!out) {
        log.error(_("Can't write '$1'.", filename));
        return false;
    }
    else
        log("Writing '%s'", filename.c_str());
    out << "; " << _("This file is automatically rewritten whenever the ban list changes.") << '\n'
        << "; " << _("To reserve a name add a row:") << '\n'
        << "; " << _("user <name> <tab> <password>  or  admin <name> [<tab> <password>]") << '\n'
        << "; " << _("where <tab> is a tabulator character.") << '\n'
        << "; " << _("Passwordless admins need to authenticate by logging in to the tournament.") << '\n'
        << '\n';
    for (vector<BanEntry>::const_iterator bi = bans.begin(); bi != bans.end(); ++bi)
        if (bi->endTime > time(0))  // if the ban isn't in effect any more, don't save
            out << "ban\t" << bi->name << '\t' << addressToString(bi->address) << '\t' << bi->endTime << '\n';
    for (vector<NameEntry>::const_iterator ni = names.begin(); ni != names.end(); ++ni)
        out << (ni->admin ? "admin" : "name") << '\t' << ni->name << '\t' << ni->password << '\n';
    return true;
}

int NameAuthorizationDatabase::identifyName(const string& name) const {
    const string nameUpr = makeComparable(name);
    for (int idx = 0; idx < (int)names.size(); ++idx)
        if (nameUpr == names[idx].name)
            return idx;
    return -1;
}

bool NameAuthorizationDatabase::isProtected(const string& name) const {
    const int idx = identifyName(name);
    return (idx != -1 && !names[idx].password.empty());
}

bool NameAuthorizationDatabase::checkNamePassword(const std::string& name, const std::string& password) const {
    const int idx = identifyName(name);
    return (idx == -1 || names[idx].password == password);
}

bool NameAuthorizationDatabase::isAdmin(const std::string& name) const {
    const int idx = identifyName(name);
    return (idx != -1 && names[idx].admin);
}

bool NameAuthorizationDatabase::isBanned(NLaddress addr) const {
    nlSetAddrPort(&addr, 0);
    for (vector<BanEntry>::const_iterator bi = bans.begin(); bi != bans.end(); ++bi)
        if (nlAddrCompare(&addr, &bi->address) && bi->endTime > time(0))
            return true;
    return false;
}

void NameAuthorizationDatabase::ban(NLaddress addr, const string& name, int minutes) {
    nlSetAddrPort(&addr, 0);
    bans.push_back(BanEntry(name, addr, time(0) + minutes * 60));
}
