/*
 *  colour.h
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

#ifndef COLOUR_H_INC
#define COLOUR_H_INC

#include <string>
#include <vector>

#include "incalleg.h"
#include "log.h"

/** RGB composite colour
 */
class Colour {
public:
    Colour() throw () : r(0), g(0), b(0), col_value(0) { }
    Colour(int r_, int g_, int b_) throw () : r(r_), g(g_), b(b_) { update(); }

    /** Recalculate the colour index from the components.
     */
    void update() throw () { col_value = makecol(r, g, b); }

    int red  () const throw () { return r; }
    int green() const throw () { return g; }
    int blue () const throw () { return b; }

    operator int() const throw () { return col_value; }

    /** Get the colour as hexadecimal values in the RRGGBB format
     */
    std::string triplet() const throw ();

    #define COL(key, r, g, b) key,
    #define SECTION(title)
    #define COMMENT(text)
    enum Col_id {
        #include "colour_def.inc"
        colours_total
    };
    #undef COL
    #undef SECTION
    #undef COMMENT

private:
    int r, g, b;
    int col_value;
};

class Colour_manager {
public:
    Colour_manager(LogSet logs) throw () :
        log(logs)
    { }

    /** Load colours from the file. If some colour is not specified there,
     *  the default colour is used.
     *  @param file filename; if empty, use default colours
     */
    void init(const std::string& file = "") throw () {
        init(file, false);
    }

    /** Create a colour file with default colours.
     *  @param file filename
     */
    void create_default_file(const std::string& file) throw () {
        init(file, true);
    }

    /** Recalculate the colour values. Use when the graphics mode changes.
     */
    void update() throw ();

    /** Get the colour value by key.
     *  @param key colour key
     */
    const Colour& operator[](Colour::Col_id key) const throw ();

private:
    void init(const std::string& file, bool create_default_only) throw ();

    std::vector<Colour> colour_set;

    mutable LogSet log;
};

#endif // COLOUR_H_INC
