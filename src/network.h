/*
 *  network.h
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

#ifndef NETWORK_H_INC
#define NETWORK_H_INC

#include <iostream>
#include <string>

#include <nl.h>

class LogSet;

const char* getNlErrorString();

bool isValidIP(const std::string& address, bool allowPort = false, unsigned int minimumPort = 0, bool requirePort = false);
bool check_private_IP(const std::string& address, bool allowAnyExternal = false);   // with allowAnyExternal only (invalid and) loopback addresses are blocked
std::string getPublicIP(LogSet& log, bool allowAnyExternal = false);    // with allowAnyExternal only (invalid and) loopback addresses are blocked
bool isLocalIP(NLaddress address);  // returns true if address points to this machine (nothing to do with the address being private)
std::string addressToString(const NLaddress& address);
inline bool operator==(const NLaddress& a1, const NLaddress& a2) { return nlAddrCompare(&a1, &a2); }

inline void readStr(const char* buf, int& count, std::string& dst) {
    dst.clear();
    while (buf[count])
        dst += buf[count++];
    ++count;
}
inline void writeStr(char* buf, int& count, const std::string& src) {
    memcpy(&buf[count], src.data(), src.length());
    count += src.length();
    buf[count++] = '\0';
}

inline double safeReadFloat(const char* buf, int& count) {
    float val;
    readFloat(buf, count, val);
    return val;
}
inline void safeWriteFloat(char* buf, int& count, float val) {  // this adds type safety: val is ensured to be a float
    writeFloat(buf, count, val);
}

enum NetworkResult { NR_ok, NR_timeout, NR_nlError };   // timeout is also returned when abortFlag triggers the return

NetworkResult writeToUnblockingTCP(NLsocket& socket, const char* data, int length,
                                const volatile bool* abortFlag, int timeout, int roundDelay = 500);
NetworkResult saveAllFromUnblockingTCP(NLsocket& socket, std::ostream& out,
                                const volatile bool* abortFlag, int timeout, int roundDelay = 500);

std::string url_encode(const std::string& str);
void url_encode(char c, std::ostream& out);
bool is_url_safe(char c);
std::string base64_encode(const std::string& data);

#endif
