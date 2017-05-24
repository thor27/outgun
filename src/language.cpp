/*
 *  language.cpp
 *
 *  Copyright (C) 2004, 2006 - Jani Rivinoja
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

#include "incalleg.h"

#include <fstream>

#include "commont.h"
#include "log.h"
#include "utility.h"

#include "language.h"

using std::ifstream;
using std::map;
using std::string;

Language language;

string Language::get_text(const string& text) const throw () {
    const map<string, string>::const_iterator translation = texts.find(text);
    if (translation == texts.end())
        return text;
    return translation->second;
}

bool Language::load(const string& lang, LogSet& log) throw () {
    texts.clear();
    lang_code = "en";
    loc = "C";
    if (lang == lang_code)  // English - no need to load the same phrases as in the code.
        return true;
    const string dir = wheregamedir + "languages" + directory_separator;
    const string defname = dir + "en.txt";
    const string translname = dir + lang + ".txt";
    ifstream def(defname.c_str());
    if (!def) {
        log.error(defname + " not found. Can't load a language without the English reference. Continuing without translation.");
        return false;
    }
    ifstream transl(translname.c_str());
    if (!transl) {
        log.error("Language file for '" + lang + "' (" + translname + ") not found. Continuing without translation.");
        return false;
    }
    for (;;) {
        string key, value;
        getline_skip_comments(def, key);
        getline_skip_comments(transl, value);
        if (!def || !transl)
            break;
        if (!value.compare(0, 9, "@MISSING@"))
            continue;
        if (key == "locale")
            loc = value;
        else
            texts[key] = value;
    }
    if (def || transl) {
        log.error("Language file for '" + lang + "' is invalid, maybe for another version of Outgun. " +
                  translname + " contains " + (transl ? "more" : "less") + " phrases than " + defname + ". Continuing without translation.");
        texts.clear();
        return false;
    }
    lang_code = lang;
    log("Language '%s' loaded", lang.c_str());
    return true;
}

string _(const string& text) throw () {
    return language.get_text(text);
}

string _(string text, const string& t1, const string& t2, const string& t3, const string& t4, const string& t5) throw () {
    text = _(text);
    const int nReplacements = 5;
    const string* const replacement[nReplacements] = { &t1, &t2, &t3, &t4, &t5 };
    string::size_type pos = 0;
    while ((pos = text.find('$', pos)) != string::npos) {
        if (pos + 1 == string::npos)
            break;
        const int val = atoi(text.substr(pos + 1, 1)) - 1;
        if (val >= 0 && val < nReplacements) {
            text.replace(pos, 2, *replacement[val]);
            pos += replacement[val]->length();
        }
    }
    return text;
}
