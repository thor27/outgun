/*
 *  mappic.h
 *
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

#ifndef MAPPIC_H_INC
#define MAPPIC_H_INC

#include <vector>
#include <string>

#include "utility.h"

class Map;

class Mappic {
public:
    class Save_error { };

    Mappic(LogSet logs) throw () : log(logs) { }

    void run() throw (Save_error);

private:
    mutable LogSet log;
    std::vector<std::string> smaps; // server maps

    std::vector<std::string> load_maps(const std::string& dir) throw ();
    void save_pictures() const throw (Save_error);
};

#endif // MAPPIC_H_INC
