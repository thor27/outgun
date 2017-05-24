/*
 *  colour.cpp
 *
 *  Copyright (C) 2008 - Jani Rivinoja
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
#include <iomanip>
#include <sstream>
#include <memory>

#include "commont.h"
#include "language.h"
#include "nassert.h"
#include "version.h"

#include "colour.h"

using std::hex;
using std::ifstream;
using std::ios;
using std::istringstream;
using std::left;
using std::max;
using std::ofstream;
using std::ostringstream;
using std::right;
using std::setfill;
using std::setw;
using std::string;
using std::uppercase;
using std::vector;

string Colour::triplet() const throw () {
    ostringstream ost;
    ost.fill('0');
    ost << hex << uppercase << setw(2) << r;
    ost << hex << uppercase << setw(2) << g;
    ost << hex << uppercase << setw(2) << b;
    return ost.str();
}

struct Colour_setting_base {
    Colour_setting_base(const string& n) throw () : name(n) { }
    virtual ~Colour_setting_base() throw () { };
    string name;
};

struct Colour_setting : public Colour_setting_base {
    Colour_setting(const string& n, int k, const Colour& c) throw () : Colour_setting_base(n), key(k), col(c) { }
    Colour_setting(const string& n, int k, int r, int g, int b) throw () : Colour_setting_base(n), key(k), col(r, g, b) { }
    int key;
    Colour col;
};

// Helper structs for commenting the colour file

struct Colour_setting_comment : public Colour_setting_base {
    Colour_setting_comment(const string& comment) throw () : Colour_setting_base(comment) { }
};

struct Colour_setting_section : public Colour_setting_comment {
    Colour_setting_section(const string& title) throw () : Colour_setting_comment(title) { }
};

void Colour_manager::init(const string& file, bool create_default_only) throw () {
    typedef std::auto_ptr<Colour_setting_base> PT;
    PT hack(0); // avoid GCC bug http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12883

    #define COL(key, r, g, b) PT(new Colour_setting(#key, Colour::key, r, g, b)),
    #define SECTION(title) PT(new Colour_setting_section(title)),
    #define COMMENT(text) PT(new Colour_setting_comment(text)),
    PT settings[] = {
        #include "colour_def.inc"
        PT(0)
    };
    #undef COL
    #undef SECTION
    #undef COMMENT

    vector<Colour> temp;
    vector<Colour>& colours = create_default_only ? temp : colour_set;  // Don't change colours when only saving the default ones.
    colours.clear();
    colours.resize(Colour::colours_total);

    string::size_type longest_key_length = 0; // This is for nice alignment of the default colour file if that needs to be created.
    for (unsigned si = 0; &*settings[si]; si++)
        if (Colour_setting* s = dynamic_cast<Colour_setting*>(&*settings[si])) {
            colours[s->key] = s->col;
            longest_key_length = max(longest_key_length, s->name.length());
        }

    if (file.empty()) {
        nAssert(!create_default_only);
        return;
    }

    if (create_default_only) {
        // Create default file
        ofstream out(file.c_str());
        if (!out)
            return;
        out <<  "; This file contains the default colour definitions of Outgun " << getVersionString() << ".\n"
                "; Each line has the colour key and value. Colours can be in format\n"
                "; of RRGGBB or RGB (red, green, blue) with hexadecimal values.\n"
                "; For example, red is FF0000, green 00FF00, yellow FFFF00 and white FFFFFF.\n"
                "; In the shorter notation, red F00, green 0F0, yellow FF0 and white FFF.\n";
        for (unsigned i = 0; &*settings[i]; i++) {
            if (Colour_setting* s = dynamic_cast<Colour_setting*>(&*settings[i])) {
                out << left << setw(longest_key_length + 1) << s->name << right;
                out << s->col.triplet() << '\n';
            }
            else if (Colour_setting_section* sect = dynamic_cast<Colour_setting_section*>(&*settings[i]))
                out << '\n' << "; " << sect->name << '\n';
            else if (Colour_setting_comment* c = dynamic_cast<Colour_setting_comment*>(&*settings[i]))
                out << "; " << c->name << '\n';
        }
        return;
    }

    ifstream in(file.c_str());
    string line;
    while (getline_skip_comments(in, line)) {
        string key, rgb;
        istringstream ist(line);
        ist >> key >> rgb;
        if (ist && (rgb.size() == 6 || rgb.size() == 3)) {
            const int len = rgb.size() / 3;
            int triplet[3];
            for (int i = 0; i < 3; ++i) {
                triplet[i] = strtol(rgb.substr(i * len, len).c_str(), 0, 16);
                if (len == 1)
                    triplet[i] *= 0x11;
            }
            bool key_found = false;
            for (unsigned si = 0; &*settings[si]; si++)
                if (Colour_setting* s = dynamic_cast<Colour_setting*>(&*settings[si]))
                    if (s->name == key) {
                        colours[s->key] = Colour(triplet[0], triplet[1], triplet[2]);
                        key_found = true;
                        break;
                    }
            if (!key_found)
                log("Unknown key in colours.txt: %s", key.c_str());
        }
        else
            log.error(_("Syntax error in $1, line '$2'.", "colours.txt", line));
    }
}

void Colour_manager::update() throw () {
    for (vector<Colour>::iterator ci = colour_set.begin(); ci != colour_set.end(); ++ci)
        ci->update();
}

const Colour& Colour_manager::operator[](Colour::Col_id key) const throw () {
    numAssert2(key >= 0 && key < int(colour_set.size()), key, colour_set.size());
    return colour_set[key];
}
