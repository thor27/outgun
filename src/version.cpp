/*
 *  version.cpp
 *
 *  Copyright (C) 2008 - Niko Ritari
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

#include <string>

#include "nassert.h"
#include "version.h"

#include "generated/svnrevision.inc"

using std::string;

const string GAME_BRANCH = "base";
/* To keep the entry in the server list menu nice, GAME_RELEASED_VERSION_SHORT should be at most 7 characters, 8 is borderline acceptable.
 * If at some point this requirement is relaxed, all uses of getVersionString must be checked for their hard limit.
 * It should fully indentify the release (within the GAME_BRANCH). It must not contain spaces.
 */
const string GAME_RELEASED_VERSION_SHORT = "1.0.3";
const string GAME_RELEASED_VERSION = "1.0.3";
const string GAME_COPYRIGHT_YEAR = "2008";

string getVersionString(bool allowSpaces, string::size_type softLimit, string::size_type hardLimit, bool tryHardForSoft) throw () {
    static const string vShort = GAME_RELEASED_VERSION_SHORT, vFull = GAME_RELEASED_VERSION, rev = SVN_REVISION;
    string ver;

    nAssert(!hardLimit || vShort.length() <= hardLimit);
    if (hardLimit && (!softLimit || softLimit > hardLimit))
        softLimit = hardLimit;

    if (rev.empty())
        return allowSpaces && (!softLimit || vFull.length() <= softLimit) ? vFull : vShort;

    if (allowSpaces) {
        ver = vFull + " r" + rev;
        if (!softLimit || ver.length() <= softLimit)
            return ver;
    }

    ver = vShort + 'r' + rev;
    if (!softLimit || ver.length() <= softLimit)
        return ver;

    ver = vShort + (rev.find_first_of("MS:") == string::npos ? '+' : 'x');
    const string::size_type limit = tryHardForSoft ? softLimit : hardLimit;
    if (!limit || ver.length() <= limit)
        return ver;

    return vShort;
}
