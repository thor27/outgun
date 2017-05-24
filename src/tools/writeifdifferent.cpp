/*
 *  writeifdifferent.cpp
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

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using std::cerr;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::ostringstream;
using std::string;

int main(int argc, const char* argv[]) {
    if (argc <= 2) {
        cerr << "syntax: " << argv[0] << " <targetfile> <new file content string>\n";
        return 2;
    }
    const string filename = argv[1];
    string newContent;
    for (int argi = 2; argi < argc; ++argi) {
        if (argi > 2)
            newContent += ' ';
        newContent += argv[argi];
    }
    newContent += '\n';

    {
        ifstream fi(filename.c_str());
        ostringstream fileContent;
        std::copy(std::istreambuf_iterator<char>(fi), std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(fileContent));
        if (fileContent.str() == newContent)
            return 0;
    }

    ofstream fo(filename.c_str());
    if (!fo) {
        cerr << argv[0] << ": can't open " << filename << " for writing\n";
        return 1;
    }
    std::copy(newContent.begin(), newContent.end(), std::ostreambuf_iterator<char>(fo));
    return 0;
}
