/*
 *  auth.cpp
 *
 *  Copyright (C) 2003, 2004, 2006, 2008 - Niko Ritari
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

#include "commont.h"
#include "language.h"
#include "nassert.h"
#include "network.h"

#include "auth.h"

using std::ifstream;
using std::istream;
using std::istringstream;
using std::map;
using std::ofstream;
using std::ostream;
using std::string;
using std::vector;

bool AuthorizationDatabase::AccessDescriptor::GamemodAccessDescriptor::isAllowed(const string& category, const string& setting) const throw () {
    bool allow = defaultAllow;
    for (vector<ControlLine>::const_iterator li = lines.begin(); li != lines.end(); ++li)
        if (li->name == category || li->name == setting)
            allow = li->allow;
    return allow;
}

ostream& AuthorizationDatabase::AccessDescriptor::GamemodAccessDescriptor::output(ostream& os) const throw () {
    os << (defaultAllow ? '+' : '-') << "gamemod";
    for (vector<ControlLine>::const_iterator li = lines.begin(); li != lines.end(); ++li)
        os << ' ' << (li->allow ? '+' : '-') << li->name;
    return os;
}

void AuthorizationDatabase::AccessDescriptor::read(istream& in, SettingChecker& validityChecker) throw (FileError) {
    for (;;) {
        string token;
        in >> token;
        if (!in)
            return;
        bool allow;
        if (token[0] == '+')
            allow = true;
        else if (token[0] == '-')
            allow = false;
        else
            throw FileError(_("Invalid authorization class entry in auth.txt: \"$1\"", token));
        token.erase(0, 1);
        if (token == "all") {
            gamemod = GamemodAccessDescriptor(allow);
            resetAccess = allow;
        }
        else if (token == "gamemod")
            gamemod = GamemodAccessDescriptor(allow);
        else if (token == "reset")
            resetAccess = allow;
        else if (validityChecker(token))
            gamemod.addLine(allow, token);
        else
            throw FileError(_("Invalid authorization class entry in auth.txt: \"$1\"", token));
    }
}

string AuthorizationDatabase::makeComparable(const string& name) throw () {
    string nameUpr;
    for (string::size_type i = 0; i < name.length(); ++i) {
        if (isalnum(name[i]))
            nameUpr += char(tolower(name[i]));
        else if (!nameUpr.empty())
            nameUpr += '.';
    }
    const string::size_type endi = nameUpr.find_last_not_of('.');
    if (endi == string::npos)
        return string();
    return nameUpr.substr(0, endi + 1);
}

void AuthorizationDatabase::clear() throw () {
    classes.clear();
    names.clear();
    bans.clear();
}

void AuthorizationDatabase::load(SettingChecker& validityChecker) throw (FileError) {
    clear();
    const string filename = wheregamedir + "config" + directory_separator + "auth.txt";
    ifstream in(filename.c_str());
    if (!in)
        throw FileError(_("Can't read '$1'.", filename));

    classes["user"] = AccessDescriptor(false, false);
    classes["local"] = classes["shell"] = AccessDescriptor(true, true);
    {
        AccessDescriptor defaultAdminAccess(false, true);
        defaultAdminAccess.gamemod.addLine(true, "bots");
        classes["admin"] = defaultAdminAccess;
    }

    bool bansChanged = false;
    for (;;) {
        string line;
        if (!getline_skip_comments(in, line))
            break;
        istringstream strl(line);
        string command, name, data;
        strl >> command;
        command = tolower(command);
        strl.ignore();  // useful especially when the separator is a tab
        getline(strl, name, '\t');
        if (strl && command == "class") {
            if (name.empty())
                throw FileError(_("Class command without class name in auth.txt."));
            name = tolower(name);
            AccessDescriptor access(false, name != "user");
            access.read(strl, validityChecker);
            classes[name] = access;
            continue;
        }
        const string compName = makeComparable(name);
        if (!strl || compName.empty())
            throw FileError(_("Invalid line (no name) in auth.txt: \"$1\"", line));
        strl >> data;
        if (command == "ban") {
            if (!isValidIP(data))
                throw FileError(_("Invalid ban command (IP address) in auth.txt: \"$1\"", line));
            else {
                Network::Address addr;
                addr.fromValidIP(data);
                nAssert(addr.valid());
                addr.setPort(0);
                time_t endTime;
                strl >> endTime;
                if (!strl)
                    bans.push_back(BanEntry(name, addr)); // use name instead of compName because names in bans don't need to be compared
                else if (endTime > time(0))
                    bans.push_back(BanEntry(name, addr, endTime));
                else    // if the ban isn't in effect any more, remove from the list
                    bansChanged = true;
            }
            continue;
        }
        const string& classId = command;
        const bool admin = classId != "user";
        if (data.empty() && !admin)
            throw FileError(_("Invalid user command (no password) in auth.txt: \"$1\"", line));
        map<string, AccessDescriptor>::const_iterator ci = classes.find(classId);
        if (ci == classes.end())
            throw FileError(_("Unrecognized command \"$1\" in auth.txt", classId));
        AccessDescriptor access = ci->second;
        access.admin = admin;
        access.hasPassword = !data.empty();
        names.push_back(NameEntry(compName, data, access, classId));
    }
    if (bansChanged)
        save();
}

void AuthorizationDatabase::save() const throw (FileError) {
    const string filename = wheregamedir + "config" + directory_separator + "auth.txt";
    ofstream out(filename.c_str());
    if (!out)
        throw FileError(_("Can't write '$1'.", filename));
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
            out << "ban\t" << bi->name << '\t' << bi->address.toString() << '\t' << bi->endTime << '\n';
    for (map<string, AccessDescriptor>::const_iterator ci = classes.begin(); ci != classes.end(); ++ci) {
        out << "class\t" << ci->first << '\t';
        const AccessDescriptor& a = ci->second;
        out << a.gamemod << ' ' << (a.resetAccess ? '+' : '-') << "reset" << '\n';
    }
    for (vector<NameEntry>::const_iterator ni = names.begin(); ni != names.end(); ++ni)
        out << ni->classId << '\t' << ni->name << '\t' << ni->password << '\n';
    if (!out)
        throw FileError(_("Can't write '$1'.", filename));
}

int AuthorizationDatabase::identifyName(const string& name) const throw () {
    const string nameUpr = makeComparable(name);
    for (int idx = 0; idx < (int)names.size(); ++idx)
        if (nameUpr == names[idx].name)
            return idx;
    return -1;
}

AuthorizationDatabase::AccessDescriptor AuthorizationDatabase::nameAccess(const string& name) const throw () {
    const int idx = identifyName(name);
    if (idx == -1)
        return defaultAccess();
    return names[idx].access;
}

bool AuthorizationDatabase::checkNamePassword(const string& name, const string& password) const throw () {
    const int idx = identifyName(name);
    return (idx == -1 || names[idx].password == password);
}

bool AuthorizationDatabase::isBanned(Network::Address addr) const throw () {
    addr.setPort(0);
    for (vector<BanEntry>::const_iterator bi = bans.begin(); bi != bans.end(); ++bi)
        if (addr == bi->address && bi->endTime > time(0))
            return true;
    return false;
}

void AuthorizationDatabase::ban(Network::Address addr, const string& name, int minutes) throw () {
    addr.setPort(0);
    bans.push_back(BanEntry(name, addr, time(0) + minutes * 60));
}
