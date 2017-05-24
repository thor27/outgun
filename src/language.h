/*
 *  language.h
 *
 *  Copyright (C) 2004, 2006 - Jani Rivinoja
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

#ifndef LANGUAGE_H_INC
#define LANGUAGE_H_INC

#include <string>
#include <map>

class LogSet;

class Language {
public:
    Language() throw () : lang_code("en"), loc("C") { }
    ~Language() throw () { }

    bool load(const std::string& lang, LogSet& log) throw ();

    std::string get_text(const std::string& text) const throw ();
    std::string code() const throw () { return lang_code; }

    std::string locale() const throw () { return loc; }

private:
    std::map<std::string, std::string> texts;
    std::string lang_code;
    std::string loc;
};

extern Language language;

std::string _(const std::string& text) throw ();

// Get translation and replace $1...$5 with t1...t5.
std::string _(std::string text, const std::string& t1,
                                const std::string& t2 = "$2",
                                const std::string& t3 = "$3",
                                const std::string& t4 = "$4",
                                const std::string& t5 = "$5") throw ();

#endif
