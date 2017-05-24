/*
 *  platform.h
 *
 *  Copyright (C) 2004, 2006, 2008 - Niko Ritari
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

#ifndef PLATFORM_H_INC
#define PLATFORM_H_INC

#include <cstdarg>
#include <string>

#include "utility.h"

int platStricmp(const char* s1, const char* s2) throw ();
int platVsnprintf(char* buf, size_t count, const char* fmt, va_list arg) throw () PRINTF_FORMAT(3, 0);
void platMessageBox(const std::string& caption, const std::string& text, bool blocking) throw (); // blocking may not be controllable

inline int platSnprintf(char* buf, size_t count, const char* fmt, ...) throw () PRINTF_FORMAT(3, 4);

inline int platSnprintf(char* buf, size_t count, const char* fmt, ...) throw () {
    va_list args;
    va_start(args, fmt);
    int ret = platVsnprintf(buf, count, fmt, args);
    va_end(args);
    return ret;
}

class FileFinder {
public:
    virtual ~FileFinder() throw () { }
    virtual bool hasNext() const throw () = 0;
    virtual std::string next() throw () = 0; // only call after hasNext() returning true
};

/** Create a FileFinder object to list matching files in a directory.
 * @param path         the directory to search in (can't include wildcards)
 * @param extension    a string of characters to be found at the end of a filename for it to be matched (can't include wildcards or '/' or '\'): include . if you want to limit to a strict extension
 * @param directories  if true, matched files are directories; if false, regular files
 */
ControlledPtr<FileFinder> platMakeFileFinder(const std::string& path, const std::string& extension, bool directories) throw ();

int platMkdir(const std::string& path) throw ();

bool platIsFile(const std::string& name) throw (); // returns true if name exists and is not a directory
bool platIsDirectory(const std::string& name) throw ();

void platInit() throw (); // perform platform specific initializations; called very early in the program
void platInitAfterAllegro() throw (); // second stage initializations, when Allegro is running (or won't be at all)

void platUninit() throw (); // clean up; called before exiting

#endif
