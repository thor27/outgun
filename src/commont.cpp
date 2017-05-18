/*
 *  commont.cpp
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2006 - Jani Rivinoja
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

#include <iostream>
#include <string>

#include <cstdlib>

#include "incalleg.h"
#include "commont.h"
#include "language.h"
#include "nassert.h"
#include "network.h"
#include "timer.h"
#include "utility.h"

using std::cout;
using std::ifstream;
using std::istream;
using std::string;

#ifndef DEDICATED_SERVER_ONLY

bool readJoystickButton(int button) {
    return (button > 0 && !poll_joystick() && button <= joy[0].num_buttons && joy[0].button[button - 1].b);
}

bool is_keypad(int sc) {
    switch (sc) {
    /*break;*/ case KEY_1_PAD: case KEY_2_PAD: case KEY_3_PAD: case KEY_4_PAD: case KEY_5_PAD: case KEY_6_PAD: case KEY_7_PAD: case KEY_8_PAD: case KEY_9_PAD: return true;
        break; default: return false;
    }
}

void ClientControls::fromKeyboard(bool use_pad, bool use_cursor_keys) {
    if (use_cursor_keys) {
        if (key[KEY_UP])
            data |= up;
        if (key[KEY_DOWN])
            data |= down;
        if (key[KEY_LEFT])
            data |= left;
        if (key[KEY_RIGHT])
            data |= right;
    }
    if (use_pad) {
        if (key[KEY_8_PAD] && use_pad)
            data |= up;
        if (key[KEY_2_PAD] || key[KEY_5_PAD])
            data |= down;
        if (key[KEY_4_PAD])
            data |= left;
        if (key[KEY_6_PAD])
            data |= right;
        if (key[KEY_7_PAD])
            data |= up | left;
        if (key[KEY_9_PAD])
            data |= up | right;
        if (key[KEY_1_PAD])
            data |= down | left;
        if (key[KEY_3_PAD])
            data |= down | right;
    }
    if (key[KEY_LSHIFT] || key[KEY_RSHIFT])
        data |= run;
    if (key[KEY_ALT] || key[KEY_ALTGR])
        data |= strafe;
}

void ClientControls::fromJoystick(int moving_stick, int run_button, int strafe_button) {
    if (poll_joystick())
        return;     // failure
    const JOYSTICK_INFO& joystick = joy[0];
    if (joystick.num_sticks > moving_stick) {
        if (joystick.stick[moving_stick].num_axis >= 2) {
            if (joystick.stick[moving_stick].axis[0].d1)
                data |= left;
            if (joystick.stick[moving_stick].axis[0].d2)
                data |= right;
            if (joystick.stick[moving_stick].axis[1].d1)
                data |= up;
            if (joystick.stick[moving_stick].axis[1].d2)
                data |= down;
        }
    }
    if (readJoystickButton(run_button))
        data |= run;
    if (readJoystickButton(strafe_button))
        data |= strafe;
}

#endif // DEDICATED_SERVER_ONLY

istream& getline_smart(istream& in, string& str) {
    str.clear();
    while (1) {
        const char c = in.get();
        if (!in) {
            if (!str.empty())
                in.clear();
            return in;
        }
        if (c == '\n' || c == '\r') {
            if (str.empty())
                continue;
            else
                return in;
        }
        str += c;
    }
}

istream& getline_skip_comments(istream& in, string& str) {
    while (getline_smart(in, str))
        if (str[0] != ';')  // str is never empty when getline_smart succeeds
            return in;
    return in;
}

bool check_name(const std::string& name) {
    if (name.length() > maxPlayerNameLength)
        return false;
    if (name.find_first_not_of(" \xA0") == string::npos)    // Name with only spaces and no-brake spaces not allowed.
        return false;
    if (find_nonprintable_char(name))
        return false;
    if (name.find_first_of(" \xA0") == 0 || name.find_last_of(" \xA0") == name.length() - 1)
        return false;
    return true;
}

bool isFlood(const string& message) {
    int count = 0;
    string::value_type chr = 0;
    for (string::const_iterator s = message.begin(); s != message.end(); ++s) {
        if (chr != *s) {
            chr = *s;
            count = 1;
        }
        else if (++count == 7)
            return true;
    }
    return false;
}

#ifndef DEDICATED_SERVER_ONLY

volatile bool GlobalDisplaySwitchHook::flag = false;

void GlobalDisplaySwitchHook__callback() {
    GlobalDisplaySwitchHook::flag = true;
} END_OF_FUNCTION(GlobalDisplaySwitchHook__callback)

void GlobalDisplaySwitchHook::init() {
    LOCK_VARIABLE(flag);
    LOCK_FUNCTION(GlobalDisplaySwitchHook__callback);
    flag = false;
}

void GlobalDisplaySwitchHook::install() {
    set_display_switch_callback(SWITCH_IN, GlobalDisplaySwitchHook__callback);
}

bool GlobalDisplaySwitchHook::readAndClear() {
    const bool f = flag;
    if (f)
        flag = false;
    return f;
}

#endif // DEDICATED_SERVER_ONLY

void MasterSettings::load(LogSet& log) {
    static const char* defaultName = "koti.mbnet.fi";
    static const char* defaultIP = "194.100.161.5";
    static const int defaultPort = 80;
    static const char* defaultQueryScript = "/outgun/servers/";
    static const char* defaultSubmitScript = "/outgun/servers/submit.php";
    static const char* defaultBugName = "-";
    static const char* defaultBugIP = "130.233.18.23";
    static const int defaultBugPort = 24900;

    log("Reading config/master.txt");
    ifstream in((wheregamedir + "config" + directory_separator + "master.txt").c_str());

    string name, ip, bugName, bugIP;
    if (!getline_skip_comments(in, name))
        name = defaultName;
    if (!getline_skip_comments(in, ip))
        ip = defaultIP;
    else if (!isValidIP(ip, true, 1)) {
        log.error(_("'$1', given in master.txt is not a valid IP address.", ip));
        ip = defaultIP;
    }
    if (!getline_skip_comments(in, queryScript))
        queryScript = defaultQueryScript;
    if (!getline_skip_comments(in, submitScript))
        submitScript = defaultSubmitScript;

    if (!getline_skip_comments(in, bugName))
        bugName = defaultBugName;
    if (!getline_skip_comments(in, bugIP))
        bugIP = defaultBugIP;
    else if (!isValidIP(ip, true, 1)) {
        log.error(_("'$1', given in master.txt is not a valid IP address.", bugIP));
        bugIP = defaultBugIP;
    }

    in.close();

    FILE *fp = fopen((wheregamedir + "config" + directory_separator + "master.txt").c_str(), "rb");
    if (fp) {
        static const int bufSize = 1024;    // the first kbyte should be enough to distinguish versions, even if the file at some point gets this large
        NLubyte buf[bufSize];
        const int numread = fread(buf, 1, bufSize, fp);
        fclose(fp);
        configCRC = nlGetCRC16(buf, numread);
    }
    else
        configCRC = 0;

    log("Resolving master server address...");
    try {
        if (name.length() < 3)
            masterAddress.valid = NL_FALSE;
        else
            nlGetAddrFromName(name.c_str(), &masterAddress);
        if (bugName.length() < 3)
            bugAddress.valid = NL_FALSE;
        else
            nlGetAddrFromName(bugName.c_str(), &bugAddress);
    } catch (...) {
        log("Caught exception probably on nlGetAddrFromNameAsync()");
        masterAddress.valid = bugAddress.valid = NL_FALSE;
    }

    if (masterAddress.valid == NL_FALSE) {
        if (name.length() >= 3)
            log("Can't resolve master server DNS name to IP.");
        nlStringToAddr(ip.c_str(), &masterAddress);
    }
    if (bugAddress.valid == NL_FALSE) {
        if (bugName.length() >= 3)
            log("Can't resolve bug report server DNS name to IP.");
        nlStringToAddr(bugIP.c_str(), &bugAddress);
    }
    if (nlGetPortFromAddr(&masterAddress) == 0) // port is unspecified or an error occured
        nlSetAddrPort(&masterAddress, defaultPort);
    if (nlGetPortFromAddr(&bugAddress) == 0) // port is unspecified or an error occured
        nlSetAddrPort(&bugAddress, defaultBugPort);
    log("Master server address set: %s (%s), port %d.", name.c_str(), ip.c_str(), nlGetPortFromAddr(&masterAddress));
    log("Bug report server address set: %s (%s), port %d.", bugName.c_str(), bugIP.c_str(), nlGetPortFromAddr(&bugAddress));
}

MasterSettings g_masterSettings;
