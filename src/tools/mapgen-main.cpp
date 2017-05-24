/*
 *  mapgen-main.cpp
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

#include "../mapgen.h"

using std::cout;

int main(int argc, const char* argv[]) {
    if (argc != 3)
        cout << "Usage: mapgen width height\n";
    else {
        srand(time(0));
        cout << "Width " << atoi(argv[1]) << ", height " << atoi(argv[2]) << '\n';
        MapGenerator generator;
        generator.generate(atoi(argv[1]), atoi(argv[2]), false);
        generator.draw(cout);
    }
}
