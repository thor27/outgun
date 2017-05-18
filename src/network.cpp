/*
 *  network.cpp
 *
 *  Copyright (C) 2004, 2006 - Niko Ritari
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

#include <iomanip>
#include <sstream>
#include <string>

#include <nl.h>

#include "nassert.h"
#include "utility.h"
#include "timer.h"

#include "network.h"

using std::hex;
using std::ostream;
using std::ostringstream;
using std::setfill;
using std::setw;
using std::string;

const char* getNlErrorString() {
    if (nlGetError() == NL_SYSTEM_ERROR)
        return nlGetSystemErrorStr(nlGetSystemError());
    else
        return nlGetErrorStr(nlGetError());
}

bool isValidIP(const std::string& address, bool allowPort, unsigned int minimumPort, bool requirePort) {
    unsigned int i1, i2, i3, i4, port;
    char midChar, endChar;
    const int n = sscanf(address.c_str(), "%u.%u.%u.%u%c%u%c", &i1, &i2, &i3, &i4, &midChar, &port, &endChar);
    if (allowPort && (requirePort || n != 4)) {
        if (n != 6 || midChar != ':' || port > 65535 || port < minimumPort)
            return false;
    }
    else {
        if (n != 4)
            return false;
    }
    return (i1 < 256 && i2 < 256 && i3 < 256 && i4 < 256 && (i1 + i2 + i3 + i4 != 0));
}

bool check_private_IP(const string& address, bool allowAnyExternal) {
    int i1, i2;
    const int n = sscanf(address.c_str(), "%d.%d.", &i1, &i2);
    nAssert(n == 2);
    if (n != 2)
        return true;
    // private IP ranges:
    //  10.  0.0.0  -   10.255.255.255
    // 172. 16.0.0  -  172. 31.255.255
    // 192.168.0.0  -  192.168.255.255
    // 169.254.0.0  -  169.254.255.255 [used by Microsoft DHCP client]
    // 127.  0.0.0  -  127.255.255.255 [loopback]
    if (i1 == 127)
        return true;
    if (allowAnyExternal)
        return false;
    return (i1 == 10 || (i1 == 172 && i2 >= 16 && i2 <= 31) || (i1 == 192 && i2 == 168) || (i1 == 169 && i2 == 254));
}

string getPublicIP(LogSet& log, bool allowAnyExternal) {
    NLint nLocals;
    NLaddress* locals = nlGetAllLocalAddr(&nLocals);
    for (int i = 0; i < nLocals; ++i) {
        const string addr = addressToString(locals[i]);
        if (check_private_IP(addr, allowAnyExternal))
            log("Local address %s ignored", addr.c_str());
        else {
            log("Found public address %s", addr.c_str());
            return addr;
        }
    }
    log("No public address found");
    return string();
}

bool isLocalIP(NLaddress address) { // local doesn't mean private
    nlSetAddrPort(&address, 0);
    NLaddress loopback;
    nAssert(nlStringToAddr("127.0.0.1", &loopback));
    if (nlAddrCompare(&address, &loopback))
        return true;
    NLaddress* locals;
    NLint nLocals;
    locals = nlGetAllLocalAddr(&nLocals);
    for (int i = 0; i < nLocals; ++i)
        if (nlAddrCompare(&address, &locals[i]))
            return true;
    return false;
}

string addressToString(const NLaddress& address) {
    char buf[NL_MAX_STRING_LENGTH];
    nlAddrToString(&address, buf);
    return buf;
}

NetworkResult writeToUnblockingTCP(NLsocket& socket, const char* data, int length, const volatile bool* abortFlag, int timeout, int roundDelay) {
    int at = 0;
    int tries = 0;
    while (at < length) {
        if ((abortFlag && *abortFlag) || tries * roundDelay > timeout)
            return NR_timeout;

        NLint written = nlWrite(socket, data + at, length - at);
        if (written == NL_INVALID) {
            if (nlGetError() != NL_CON_PENDING)
                return NR_nlError;
        }
        else
            at += written;

        platSleep(roundDelay);
        ++tries;
    }
    return NR_ok;
}

NetworkResult saveAllFromUnblockingTCP(NLsocket& socket, ostream& out, const volatile bool* abortFlag, int timeout, int roundDelay) {
    const int buffer_size = 511;
    char lebuf[buffer_size + 1];

    int tries = 0;
    for (;;) {
        if ((abortFlag && *abortFlag) || tries * roundDelay > timeout)
            return NR_timeout;

        NLint read = nlRead(socket, lebuf, buffer_size);
        if (read == NL_INVALID) {
            if (nlGetError() != NL_CON_PENDING)
                return (nlGetError() == NL_MESSAGE_END) ? NR_ok : NR_nlError;
        }
        else {
            lebuf[read] = '\0';
            out << lebuf;
        }

        platSleep(roundDelay);
        ++tries;
    }
}

string url_encode(const string& str) {
    ostringstream ost;
    for (string::const_iterator s = str.begin(); s != str.end(); s++)
        url_encode(*s, ost);
    return ost.str();
}

void url_encode(char c, ostream& out) {
    if (is_url_safe(c)) // send safe characters as they are
        out << c;
    else if (c == ' ')  // spaces to + characters
        out << '+';
    else                // encode unsafe characters to %xx
        out << '%' << hex << setw(2) << setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
}

bool is_url_safe(char c) {
    if (c >= 'a' && c <= 'z')
        return true;
    else if (c >= 'A' && c <= 'Z')
        return true;
    else if (c >= '0' && c <= '9')
        return true;
    const string safe_characters("$-_.+!*'(),");
    return safe_characters.find(c) != string::npos;
}

string base64_encode(const string& data) {
    const string conversion_table("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    const char padding = '=';
    string result;
    // Convert data to 6-bit sequences. Take characters for every sequence
    // from the conversion table.
    for (string::const_iterator s = data.begin(); s != data.end(); s++) {
        // first encoded byte
        char value = (*s >> 2) & 0x3F;
        result += conversion_table[value];
        // second encoded byte
        value = (*s << 4) & 0x3F;
        s++;
        if (s != data.end())
            value |= (*s >> 4) & 0x0F;
        result += conversion_table[value];
        // third encoded byte
        if (s != data.end()) {
            value = (*s << 2) & 0x3F;
            s++;
            if (s != data.end())
                value |= (*s >> 6) & 0x03;
            result += conversion_table[value];
        }
        else
            result += padding;
        // fourth encoded byte
        if (s != data.end()) {
            value = *s & 0x3F;
            result += conversion_table[value];
        }
        else
            result += padding;
    }
    return result;
}
