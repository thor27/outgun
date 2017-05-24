/*
 *  version.h
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

#ifndef VERSION_H_INC
#define VERSION_H_INC

#include <string>

extern const std::string GAME_BRANCH; // this only affects the master server communications, to make it tell the correct newest version

extern const std::string GAME_RELEASED_VERSION_SHORT; // no longer than 8 characters
extern const std::string GAME_RELEASED_VERSION;
extern const std::string GAME_COPYRIGHT_YEAR;

/** Get a version identifier string combining the GAME_RELEASED_VERSION definitions and a potential SVN revision.
 * @param allowSpaces      allow spaces in the string (prefer VERSION), otherwise use VERSION_SHORT
 * @param softLengthLimit  the maximum string length preferred
 * @param hardLengthLimit  the maximum string length accepted (must be at least the length of VERSION_SHORT)
 * @param tryHardForSoft   don't return more than /softLengthLimit/ characters unless absolutely necessary
 */
std::string getVersionString(bool allowSpaces = true, std::string::size_type softLengthLimit = 0, std::string::size_type hardLengthLimit = 0, bool tryHardForSoft = false) throw ();

#endif // ! VERSION_H_INC
